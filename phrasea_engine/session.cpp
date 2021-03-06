#include "base_header.h"

#include "../php_phrasea2.h"

ZEND_FUNCTION(phrasea_clear_cache)
{
	if(ZEND_NUM_ARGS() != 1)
		WRONG_PARAM_COUNT; // returns !

	long sesid;

	if(zend_parse_parameters(1 TSRMLS_CC, (char *) "l", &sesid) == FAILURE)
		RETURN_FALSE;

	ZVAL_BOOL(return_value, FALSE);

	SQLCONN *epublisher = PHRASEA2_G(epublisher);
	if(epublisher)
	{
		if(sesid != 0)
		{
			// check if the session exists
			char sql[65 + 20 + 1];
			sprintf(sql, "UPDATE cache SET nact=nact+1, lastaccess=NOW() WHERE session_id=%ld", sesid);
			if(epublisher->query(sql))
			{
				if(epublisher->affected_rows() == 1)
				{
					char *fname;
					int l = strlen(PHRASEA2_G(tempPath))
						+ 9 // "_phrasea."
						+ strlen(epublisher->ukey)
						+ 9 // ".answers."
						+ 33 // zsession
						+ 1; // '\0'

					if((fname = (char *) EMALLOC(l)))
					{
						sprintf(fname, "%s_phrasea.%s.answers.%ld.bin", PHRASEA2_G(tempPath), epublisher->ukey, sesid);
						remove(fname);
						sprintf(fname, "%s_phrasea.%s.spots.%ld.bin", PHRASEA2_G(tempPath), epublisher->ukey, sesid);
						remove(fname);
						EFREE(fname);
					}

					if(CACHE_SESSION * tmp_session = new CACHE_SESSION(0, epublisher))
					{
						if(tmp_session->restore(sesid))
						{
							if(PHRASEA2_G(global_session))
								delete PHRASEA2_G(global_session);
							PHRASEA2_G(global_session) = tmp_session;

							//PHRASEA2_G(global_session)->serialize_php(return_value, false); // false : do NOT include offlines and not registered

							ZVAL_BOOL(return_value, TRUE);
						}
					}
				}
			}
		}
		epublisher->close();
	}
}

ZEND_FUNCTION(phrasea_create_session)
{
	if(ZEND_NUM_ARGS() != 1)
		WRONG_PARAM_COUNT;

	long usrid;

	if(zend_parse_parameters(1 TSRMLS_CC, (char *) "l", &usrid) == FAILURE)
		RETURN_FALSE;

	ZVAL_BOOL(return_value, FALSE);

	long sesid = -1;
	char sql[96 + 20 + 1];
	SQLCONN *epublisher = PHRASEA2_G(epublisher);
	if(epublisher)
	{
		sprintf(sql, (char *) "INSERT INTO cache (session_id, nact, lastaccess, session, usr_id) VALUES (null, 0, NOW(), '', %li)", usrid);
		if(epublisher->query(sql))
		{

			SQLRES res(epublisher);
			if(res.query((char *) "SELECT LAST_INSERT_ID()"))
			{
				SQLROW *row = res.fetch_row();
				if(row)
				{
					sesid = atol(row->field(0));

					//					ZVAL_LONG(return_value, sesid);
				}
			}
		}
	}

	if(sesid != -1)
	{
		// we got a session, get list of published databases
		SQLRES res(epublisher);
		if(res.query((char *) "SELECT base_id, host, port, dbname, user, pwd, server_coll_id, sbas.sbas_id, viewname  FROM (sbas INNER JOIN bas ON sbas.sbas_id=bas.sbas_id) WHERE active>0 ORDER BY sbas.ord ASC, sbas.sbas_id ASC, bas.ord ASC, bas.server_coll_id ASC"))
		{
			long last_sbas_id = 0, sbas_id;
			char *viewname;
			SQLROW *row;
			SQLCONN *conn = NULL;
			long basid;

			if(CACHE_SESSION * tmp_session = new CACHE_SESSION(sesid, epublisher))
			{
				CACHE_BASE *cache_base = NULL;
				while(row = res.fetch_row())
				{
					basid = atol(row->field(0));
					sbas_id = atol(row->field(7));
					viewname = (row->field(8) && strlen(row->field(8)) > 0) ? row->field(8) : row->field(3);
					if(sbas_id != last_sbas_id)
					{
						last_sbas_id = sbas_id;

						// we changed of database
						if(conn)
							delete conn;

						conn = new SQLCONN(row->field(1), atoi(row->field(2)), row->field(4), row->field(5), row->field(3));
						if(conn && conn->isok())
						{
							SQLRES res2(conn);
							if(res2.query((char *) "SELECT value AS struct FROM pref WHERE prop='structure' LIMIT 1;"))
							{
								SQLROW *row2 = res2.fetch_row();
								if(row2)
									cache_base = tmp_session->addbase(basid, row->field(1), atoi(row->field(2)), row->field(4), row->field(5), row->field(3), row2->field(0), sbas_id, viewname); //, true);
								else
									cache_base = tmp_session->addbase(basid, row->field(1), atoi(row->field(2)), row->field(4), row->field(5), row->field(3), NULL, sbas_id, viewname); //, true);
							}
							else
							{
								cache_base = tmp_session->addbase(basid, row->field(1), atoi(row->field(2)), row->field(4), row->field(5), row->field(3), NULL, sbas_id, viewname); //, true);
							}
//							conn->close();
						}
						else
						{
							// if oonnection failed, don't include in the databases !
						}
					}

					// here for every base / collections
					if(conn && conn->isok())
					{
						SQLRES res(conn);
						long collid = atol(row->field(6));
						sprintf(sql, "SELECT asciiname, prefs FROM coll WHERE coll_id=%ld", collid);
						if(res.query(sql))
						{
							SQLROW *row = res.fetch_row();

							if(cache_base)
							{
								// cache_base->addcoll(collid, basid, row->field(0), (char *) (row->field(1) ? row->field(1) : ""), false);
								cache_base->addcoll(collid, basid, row->field(0), (char *) (row->field(1) ? row->field(1) : "")); // , true);
							}
						}
					}
				}

				if(conn)
					delete conn;
// tmp_session->dump();
				// keep the session global and save in cache
				if(PHRASEA2_G(global_session))
					delete PHRASEA2_G(global_session);
				PHRASEA2_G(global_session) = tmp_session;
				PHRASEA2_G(global_session)->save();

				// serialize and return the session array
				// PHRASEA2_G(global_session)->serialize_php(return_value, false); // do NOT include offlines and non registered

				// return the session number
				ZVAL_LONG(return_value, sesid);
			}
		}
	}
}

ZEND_FUNCTION(phrasea_open_session)
{
	if(ZEND_NUM_ARGS() != 2)
		WRONG_PARAM_COUNT;	// returns !

	long sesid, usrid;

	if(zend_parse_parameters(2 TSRMLS_CC, (char *) "ll", &sesid, &usrid) == FAILURE)
		RETURN_FALSE;

	ZVAL_BOOL(return_value, FALSE);

	char sql[65 + 20 + 12 + 20 + 1];
	SQLCONN *epublisher = PHRASEA2_G(epublisher);
	if(epublisher)
	{
		sprintf(sql, "UPDATE cache SET nact=nact+1, lastaccess=NOW() WHERE session_id=%li AND usr_id=%li", sesid, usrid);
		if(epublisher->query(sql))
		{
			if(epublisher->affected_rows() == 1)
			{
				if(CACHE_SESSION *tmp_session = new CACHE_SESSION(0, epublisher))
				{
					if(tmp_session->restore(sesid))
					{
						if(tmp_session->get_session_id() == sesid)
						{
							if(PHRASEA2_G(global_session))
								delete PHRASEA2_G(global_session);
							PHRASEA2_G(global_session) = tmp_session;

							PHRASEA2_G(global_session)->serialize_php(return_value); //, false); // do NOT include offlines and non registered
						}
					}
				}
			}
		}
		epublisher->close();
	}
}

ZEND_FUNCTION(phrasea_close_session)
{
	if(ZEND_NUM_ARGS() != 1)
		WRONG_PARAM_COUNT;	// returns !

	long sesid;

	if(zend_parse_parameters(1 TSRMLS_CC, (char *) "l", &sesid) == FAILURE)
		RETURN_FALSE;

	ZVAL_BOOL(return_value, FALSE);

	char sql[36 + 20 + 1];
	SQLCONN *epublisher = PHRASEA2_G(epublisher);
	if(epublisher)
	{
		// delete session from cache
		sprintf(sql, "DELETE FROM cache WHERE session_id=%li", sesid);
		if(epublisher->query(sql))
		{
			if(epublisher->affected_rows() == 1)
			{
				char *fname;
				int l = strlen(PHRASEA2_G(tempPath))
					+ 9 // "_phrasea."
					+ strlen(epublisher->ukey)
					+ 9 // ".answers."
					+ 33 // zsession
					+ 1; // '\0'
				if((fname = (char *) EMALLOC(l)))
				{
					sprintf(fname, "%s_phrasea.%s.answers.%ld.bin", PHRASEA2_G(tempPath), epublisher->ukey, sesid);
					remove(fname);
					sprintf(fname, "%s_phrasea.%s.spots.%ld.bin", PHRASEA2_G(tempPath), epublisher->ukey, sesid);
					remove(fname);
					EFREE(fname);
				}
				ZVAL_BOOL(return_value, TRUE);
			}
		}
		epublisher->close();
	}
}

