/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/
/*

   These are the Catalog Low Level (cll) routines for talking to postgresql.

   For each of the supported database systems there is .c file like this
   one with a set of routines by the same names.

   Callable functions:
   cllOpenEnv
   cllCloseEnv
   cllConnect
   cllDisconnect
   cllGetRowCount
   cllExecSqlNoResult
   cllExecSqlWithResult
   cllDoneWithResult
   cllDoneWithDefaultResult
   cllGetRow
   cllGetRows
   cllGetNumberOfColumns
   cllGetColumnInfo
   cllNextValueString

   Internal functions are those that do not begin with cll.
   The external functions used are those that begin with SQL.

*/

#include "low_level_libpq.hpp"

#include "irods_log.hpp"
#include "irods_stacktrace.hpp"
#include "irods_server_properties.hpp"

#include <cctype>
#include <string>
#include <boost/scope_exit.hpp>
#include <boost/algorithm/string.hpp>

#include <sys/types.h>
#include <unistd.h>
#include <fstream>

int cllBindVarCount = 0;
const char *cllBindVars[MAX_BIND_VARS];
int cllBindVarCountPrev = 0; /* cllBindVarCount earlier in processing */
std::vector<result_set *> result_sets;
static int didBegin = 0;


#define TMP_STR_LEN 1040

#include <stdio.h>
#include <pwd.h>
#include <ctype.h>

#include <vector>
#include <string>
#include <libpq-fe.h>

/*
  call SQLError to get error information and log it
*/
int
logPsgError( int level, PGresult *res ) {
    const char *sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
    const char *psgErrorMsg = PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY);

    int errorVal = -2;
            if ( strcmp( ( char * )sqlstate, "23505" ) == 0 &&
                    strstr( ( char * )psgErrorMsg, "duplicate key" ) ) {
                errorVal = CATALOG_ALREADY_HAS_ITEM_BY_THAT_NAME;
            }

        rodsLog( level, "SQLSTATE: %s", sqlstate );
        rodsLog( level, "SQL Error message: %s", psgErrorMsg );

    return errorVal;
}

/*
  Log the bind variables from the global array (after an error)
*/
void
logBindVariables( int level, const std::vector<std::string> &bindVars ) {
    for ( int i = 0; i < bindVars.size(); i++ ) {
        char tmpStr[TMP_STR_LEN + 2];
        snprintf( tmpStr, TMP_STR_LEN, "bindVar[%d]=%s", i + 1, bindVars[i].c_str() );
        rodsLog( level, "%s", tmpStr );
    }
}

result_set::result_set() : res_(nullptr), row_(0) {
}

paging_result_set::paging_result_set(std::function<int(int, int, PGresult *&)> _query, int _offset, int _maxrows) : query_(_query), offset_(_offset), maxrows_(_maxrows) {
}

all_result_set::all_result_set(std::function<int(PGresult *&)> _query) : query_(_query) {
}

result_set::~result_set() {
  clear();
}

int paging_result_set::next_row() {
   if(res_ == nullptr || row_ >= PQntuples(res_) - 1) {
     row_ = 0;
     if (res_ != nullptr) {
       offset_ += PQntuples(res_);
     }
     PQclear(res_);
     return query_(offset_, maxrows_, res_);
   } else {
     row_++;
     return 0;
   }
}

int all_result_set::next_row() {
   if(res_ == nullptr) {
     return query_(res_);
   } else if (row_ >= PQntuples(res_) - 1) {
     return CAT_SUCCESS_BUT_WITH_NO_INFO;
   } else {
     row_++;
     return 0;
   }
}

bool result_set::has_row() {
   return res_ != nullptr && PQntuples(res_) > 0;
}

int result_set::row_size() {
   return PQnfields(res_);
}

int result_set::size() {
   return PQntuples(res_);
}

void result_set::get_value(int _col, char *_buf, int _len) {
  snprintf(_buf, _len, "%s", get_value(_col));
}

const char *result_set::get_value(int _col) {
  return PQgetvalue(res_, row_, _col);
}

const char *result_set::col_name(int i) {
   return PQfname(res_, i);
}

void result_set::clear() {
  if(res_ != nullptr) {
    PQclear(res_);
    res_ = nullptr;
  }
}

std::string replaceParams(const std::string &_sql) {
  std::stringstream ss;
  int i = 1;
  for(const char &ch : _sql) {
    if (ch == '?') {
      ss << "$" << std::to_string(i++);
    } else {
      ss << ch;
    }
  }
  return ss.str();
}

std::string replaceLikesToSimilarTos(const std::string &_sql) {
  std::string res(_sql);
  boost::replace_all(res, " like ", " similar to ");
  boost::replace_all(res, " LIKE ", " SIMILAR TO ");
  return res;
}


std::tuple<int, std::string> processRes(const std::string &_sql, const std::vector<std::string> &bindVars, PGresult *res) {
      ExecStatusType stat = PQresultStatus(res);
      rodsLogSqlResult( PQresStatus(stat) );
      int result = 0;
      if ( stat == PGRES_COMMAND_OK ||
	      stat == PGRES_TUPLES_OK ) {
    	  if ( ! boost::iequals( _sql, "begin" )  &&
    		  ! boost::iequals( _sql, "commit" ) &&
    		  ! boost::iequals( _sql, "rollback" ) &&
          ! boost::iequals(_sql, "savepoint cockroach_restart") &&
          ! boost::iequals(_sql, "release savepoint cockroach_restart") &&
          ! boost::iequals(_sql, "rollback to savepoint cockroach_restart") ) {
    	      if ( atoi(PQcmdTuples(res)) == 0 ) {
              result = CAT_SUCCESS_BUT_WITH_NO_INFO;
    	      }
    	  }

        std::ofstream log("/tmp/sqllog." + std::to_string(getpid()), std::ofstream::out | std::ofstream::app);
        log << "stat = " << std::endl;
        log << "ncols = " << std::to_string(PQnfields(res)) << std::endl;
        log << "nrows = " << std::to_string(PQntuples(res)) << std::endl;
        return std::make_tuple(result, "");
      }
      else {
    	  logBindVariables( LOG_NOTICE, bindVars );
    	  rodsLog( LOG_NOTICE, "_execSql: PQexecParams error: %s sql:%s",
    		  PQresStatus(stat), _sql.c_str() );
    	  result = logPsgError( LOG_NOTICE, res );
        std::string stat = std::string(PQresultErrorField(res, PG_DIAG_SQLSTATE));
        std::ofstream log("/tmp/sqllog." + std::to_string(getpid()), std::ofstream::out | std::ofstream::app);
        log << "stat = " << stat << std::endl;
        return std::make_tuple(result, stat);
      }

}

std::tuple<int, std::string> _execTxSql(PGconn *conn, const std::string &_sql) {
  PGresult *res;
  std::ofstream log("/tmp/sqllog." + std::to_string(getpid()), std::ofstream::out | std::ofstream::app);
  log << "sql = " << _sql << std::endl;

  res = PQexec(conn, _sql.c_str());
  BOOST_SCOPE_EXIT (res) {
    PQclear(res);
  } BOOST_SCOPE_EXIT_END
  return processRes(_sql, std::vector<std::string>(), res);
}

std::tuple<bool, irods::error> _result_visitor::operator()(const irods::error &result) const
{
    return std::make_tuple(result.ok(), result);
}

std::tuple<bool, irods::error> _result_visitor::operator()(const std::tuple<bool, irods::error> & result) const
{
    return result;
}

irods::error execTx(const icatSessionStruct *icss, const boost::variant<std::function<irods::error()>, std::function<boost::variant<irods::error, std::tuple<bool, irods::error>>()>> &func) {

    PGconn *conn = (PGconn *) icss->connectPtr;

    rodsLog(LOG_NOTICE, "XXXX - Calling BEGIN :: %s:%d", __FUNCTION__, __LINE__);
    int result = std::get<0>(_execTxSql(conn, "begin"));
    if(result < 0) {
        rodsLog( LOG_NOTICE,
                "begin failure %d",
                result );
        return ERROR( result, "begin failure" );
    }

    rodsLog(LOG_NOTICE, "XXXX - Calling savepoint:: %s:%d", __FUNCTION__, __LINE__);

    result = std::get<0>(_execTxSql(conn, "savepoint cockroach_restart"));
    if(result < 0) {
        rodsLog( LOG_NOTICE,
                "savepoint cockroach_restart failure %d",
                result );
        return ERROR( result, "savepoint cockroach_restart failure" );
    }


    class _execTxSql_visitor : public boost::static_visitor<std::tuple<bool, irods::error>>
    {
        public:
            std::tuple<bool, irods::error> operator()(const std::function<irods::error()> &func) const
            {
                irods::error result4 = func();
                return std::make_tuple(result4.ok(), result4);
            }

            std::tuple<bool, irods::error> operator()(const std::function<boost::variant<irods::error, std::tuple<bool, irods::error>>()> & func) const
            {
                auto result = func();
                return boost::apply_visitor(_result_visitor(), result);
            }
    };


    rodsLog(LOG_NOTICE, "XXXX - Starting retry loop :: %s:%d", __FUNCTION__, __LINE__);

    while(true) {

        std::tuple<bool, irods::error> result4 = boost::apply_visitor(_execTxSql_visitor(), func);
        irods::error result3 = std::get<1>(result4);
        if(!std::get<0>(result4)) {
            /*  int result3 = std::get<0>(_execTxSql("rollback"));
                if(result3 < 0) {
                return CODE(result3);
                }*/
            return result3;
        }

        std::tuple<int, std::string> result2 = _execTxSql(conn, "release savepoint cockroach_restart");
        result = std::get<0>(result2);
        if(result < 0) {
            rodsLog( LOG_NOTICE,
                    "release savepoint cockroach_restart failure %d",
                    result );
            if(std::get<1>(result2) == "40001") {
                result = std::get<0>(_execTxSql(conn, "rollback to savepoint cockroach_restart"));
                if(!(result < 0)) {
                    continue;
                } else {
                    rodsLog( LOG_NOTICE,
                            "rollback to savepoint cockroach_restart failure %d",
                            result );
                }
            }
            result = std::get<0>(_execTxSql(conn, "rollback"));
            if(result < 0) {
                rodsLog( LOG_NOTICE,
                        "rollback failure %d",
                        result );
                return ERROR( result, "rollback failure" );
            }
            return ERROR(std::get<0>(result2), "release savepoint cockroach_restart failure");

        } else {
rodsLog(LOG_NOTICE, "XXXX - Calling COMMIT :: %s:%d", __FUNCTION__, __LINE__);
            result = std::get<0>(_execTxSql(conn, "commit"));
            if(result < 0) {
                rodsLog( LOG_NOTICE,
                        "commit failure %d",
                        result );
                return ERROR( result, "commit failure" );
            }
rodsLog(LOG_NOTICE, "XXXX - Done Calling COMMIT :: %s:%d", __FUNCTION__, __LINE__);
            return result3;
        }
    }
}

int _execSql(PGconn *conn, const std::string &_sql, const std::vector<std::string> &bindVars, PGresult *&res) {
      rodsLog( LOG_DEBUG10, "%s", _sql.c_str() );
      // rodsLogSql( _sql.c_str() );

      std::string sql = replaceLikesToSimilarTos(replaceParams(_sql));

      std::vector<const char *> bs;
      std::transform(bindVars.begin(), bindVars.end(), std::back_inserter(bs), [](const std::string &str){return str.c_str();});
      
      std::ofstream log("/tmp/sqllog." + std::to_string(getpid()), std::ofstream::out | std::ofstream::app);
      log << "sql = " << sql << std::endl;

      std::for_each(std::begin(bs), std::end(bs), [&](const char *param) { log << "param = " << param << std::endl;});
      res = PQexecParams( conn, sql.c_str(), bs.size(), NULL, bs.data(), NULL, NULL, 0 );
      
      log.close();

      return std::get<0>(processRes(sql, bindVars, res));
}


int
execSql(const icatSessionStruct *icss, result_set **_resset, const std::string &sql, const std::vector<std::string> &bindVars) {
    PGconn *conn = (PGconn *) icss->connectPtr;

    auto resset = new all_result_set([conn, sql, bindVars](PGresult *&res) {
      return _execSql(conn, sql, bindVars, res);
    });

    *_resset = resset;

    return resset->next_row();
}


int
execSql( const icatSessionStruct *icss, const std::string &sql, const std::vector<std::string> &bindVars) {
    result_set *resset;
    int status = execSql(icss, &resset, sql, bindVars);
    delete resset;
    return status;
}

int
execSql( const icatSessionStruct *icss, result_set **_resset, const std::function<std::string(int, int)> &_sqlgen, const std::vector<std::string> &bindVars, int offset, int maxrows) {

    PGconn *conn = (PGconn *) icss->connectPtr;

    auto resset = new paging_result_set([conn, _sqlgen, bindVars](int offset, int maxrows, PGresult *&res) {
      auto sql = _sqlgen(offset, maxrows);
      return _execSql(conn, sql, bindVars, res);
    }, offset, maxrows);

    *_resset = resset;

    return resset->next_row();
}

int cllFreeStatement(int _resinx) {
  delete result_sets[_resinx];
  result_sets[_resinx] = nullptr;
  return 0;
}




const std::string extract_optional(const std::string& key, const boost::optional<std::string> &optional_value) {
  if (optional_value != boost::none) {
    return " " + key + "=" + optional_value.value(); 
  } else {
    return "";
  }
}


/*
  Connect to the DBMS.
*/
int
cllConnect( icatSessionStruct *icss, const std::string &host, int port, const std::string &dbname, const boost::optional<std::string> &sslmode, const boost::optional<std::string> &sslrootcert, const boost::optional<std::string> &sslcert, const boost::optional<std::string> &sslkey ) {

  PGconn *conn = PQconnectdb(("host=" + host + " port=" + std::to_string(port) + " dbname=" + dbname + " user=" + icss->databaseUsername + " password=" + icss->databasePassword + extract_optional("sslmode", sslmode) + extract_optional("sslrootcert", sslrootcert) + extract_optional("sslcert", sslcert) + extract_optional("sslkey", sslkey)).c_str());

    // =-=-=-=-=-=-=-
    // initialize a connection to the catalog
    ConnStatusType stat = PQstatus(conn);
    if ( stat != CONNECTION_OK ) {
        rodsLog( LOG_ERROR, "cllConnect: SQLConnect failed: %d", stat );
        rodsLog( LOG_ERROR,
                 "cllConnect: SQLConnect failed:host=%s,port=%d,dbname=%s,user=%s,pass=XXXXX\n",
                 host.c_str(),
		 port,
		 dbname.c_str(),
                 icss->databaseUsername );
        rodsLog( LOG_ERROR, "cllConnect: %s \n", PQerrorMessage(conn) );

        PQfinish( conn );
        return -1;
    }

    icss->connectPtr = conn;

    return 0;
}

/*
  Disconnect from the DBMS.
*/
int
cllDisconnect( icatSessionStruct *icss ) {

    PGconn *conn = (PGconn *) icss->connectPtr;

    PQfinish(conn);

    return 0;
}

/*
  Bind variables from the global array.
*/
int cllGetBindVars(std::vector<std::string> &bindVars) {

    int myBindVarCount = cllBindVarCount;
    cllBindVarCountPrev = cllBindVarCount; /* save in case we need to log error */
    cllBindVarCount = 0; /* reset for next call */

    for ( int i = 0; i < myBindVarCount; ++i ) {
        char tmpStr[TMP_STR_LEN];
        snprintf( tmpStr, sizeof( tmpStr ), "bindVar[%d]=%s", i + 1, cllBindVars[i] );
        rodsLogSql( tmpStr );
        bindVars.push_back(cllBindVars[i]);
    }

    return 0;
}

/*
  Execute a SQL command which has no resulting table.  Examples include
  insert, delete, update, or ddl.
  Insert a 'begin' statement, if necessary.
*/
int
cllExecSqlNoResult( const icatSessionStruct *icss, const char *sql ) {


    std::vector<std::string> bindVars;
    if ( cllGetBindVars( bindVars ) != 0 ) {
	     return -1;
    }
    return execSql( icss, sql, bindVars);
}

int find_res_inx() {
  for(int i = 0; i < result_sets.size(); i++) {
    if(result_sets[i] == nullptr) {
      return i;
    }
  }
  int i = result_sets.size();
  result_sets.push_back(nullptr);
  return i;
}

/*
   Execute a SQL command that returns a result table, and
   and bind the default row; and allow optional bind variables.
*/
int
cllExecSqlWithResultBV(
    const icatSessionStruct *icss,
    int *_resinx,
    const char *sql,
    const std::vector< std::string > &bindVars ) {


    *_resinx = find_res_inx();

    return execSql(icss, &result_sets[*_resinx], sql, bindVars);
}

/*
   Execute a SQL command that returns a result table, and
   and bind the default row.
   This version now uses the global array of bind variables.
*/
int
cllExecSqlWithResult( const icatSessionStruct *icss, int *_resinx, const char *sql ) {
    std::vector<std::string> bindVars;
    if ( cllGetBindVars( bindVars ) != 0 ) {
        return -1;
    }

    return cllExecSqlWithResultBV(icss, _resinx, sql, bindVars);
}
