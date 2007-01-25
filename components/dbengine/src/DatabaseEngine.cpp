/*
//
// BEGIN SONGBIRD GPL
// 
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2007 POTI, Inc.
// http://songbirdnest.com
// 
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
// 
// Software distributed under the License is distributed 
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either 
// express or implied. See the GPL for the specific language 
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this 
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc., 
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
// 
// END SONGBIRD GPL
//
 */

/**
 * \file DatabaseEngine.cpp
 * \brief 
 */

// INCLUDES ===================================================================
#include "DatabaseEngine.h"

#include <xpcom/nsCOMPtr.h>
#include <xpcom/nsIFile.h>
#include <xpcom/nsILocalFile.h>
#include <nsStringGlue.h>
#include <nsThreadUtils.h>
#include <xpcom/nsIObserverService.h>
#include <xpcom/nsISimpleEnumerator.h>
#include <xpcom/nsDirectoryServiceDefs.h>
#include <xpcom/nsAppDirectoryServiceDefs.h>
#include <xpcom/nsDirectoryServiceUtils.h>
#include <nsUnicharUtils.h>

#include <nsAutoLock.h>

#include <vector>
#include <prmem.h>
#include <prtypes.h>

#if defined(_WIN32)
  #include <direct.h>
#else
// on UNIX, strnicmp is called strncasecmp
#define strnicmp strncasecmp
#endif

#define USE_SQLITE_FULL_DISK_CACHING  1
#define SQLITE_MAX_RETRIES            666
#define QUERY_PROCESSOR_THREAD_COUNT  4

#if defined(_DEBUG) || defined(DEBUG)
  #if defined(XP_WIN)
    #include <windows.h>
  #endif
  #define HARD_SANITY_CHECK             1
#endif

#ifdef PR_LOGGING
PRLogModuleInfo *gDatabaseEngineLog;
#endif

int SQLiteAuthorizer(void *pData, int nOp, const char *pArgA, const char *pArgB, const char *pDBName, const char *pTrigger)
{
  int ret = SQLITE_OK;

  CDatabaseQuery *pQuery = reinterpret_cast<CDatabaseQuery *>(pData);

  if(pQuery)
  {
    switch(nOp)
    {
      case SQLITE_CREATE_INDEX:
      {
      }
      break;

      case SQLITE_CREATE_TABLE:
      {
      }
      break;

      case SQLITE_CREATE_TEMP_INDEX:
      {
      }
      break;

      case SQLITE_CREATE_TEMP_TABLE:
      {
      }
      break;

      case SQLITE_CREATE_TEMP_TRIGGER:
      {
      }
      break;

      case SQLITE_CREATE_TEMP_VIEW:
      {
      }
      break;

      case SQLITE_CREATE_TRIGGER:
      {
      }
      break;

      case SQLITE_CREATE_VIEW:
      {
      }
      break;

      case SQLITE_DELETE:
      {
        if(pArgA && pDBName && strnicmp("sqlite_", pArgA, 7))
        {
          pQuery->m_HasChangedDataOfPersistQuery = PR_TRUE;

          {
            nsAutoLock lock(pQuery->m_pModifiedDataLock);
            nsCAutoString strDBName(pDBName);

            if(strDBName.EqualsLiteral("main"))
            {
              nsAutoString strDBGUID;
              pQuery->GetDatabaseGUID(strDBGUID);
              strDBName = NS_ConvertUTF16toUTF8(strDBGUID);
            }

            CDatabaseQuery::modifieddata_t::iterator itDB = pQuery->m_ModifiedData.find(strDBName);
            if(itDB != pQuery->m_ModifiedData.end()) {
              itDB->second.insert(nsCAutoString(pArgA));
            } else {
              CDatabaseQuery::modifiedtables_t modifiedTables;
              modifiedTables.insert(nsCAutoString(pArgA));
              pQuery->m_ModifiedData.insert(
                std::make_pair<nsCString, CDatabaseQuery::modifiedtables_t>(
                strDBName, modifiedTables));
            }
          }
        }
      }
      break;

      case SQLITE_DROP_INDEX:
      {
      }
      break;

      case SQLITE_DROP_TABLE:
      {
        if(pArgA && strnicmp("sqlite_", pArgA, 7))
        {
          pQuery->m_HasChangedDataOfPersistQuery = PR_TRUE;

          {
            nsAutoLock lock(pQuery->m_pModifiedDataLock);
            nsCAutoString strDBName(pDBName);

            if(strDBName.EqualsLiteral("main"))
            {
              nsAutoString strDBGUID;
              pQuery->GetDatabaseGUID(strDBGUID);
              strDBName = NS_ConvertUTF16toUTF8(strDBGUID);
            }

            CDatabaseQuery::modifieddata_t::iterator itDB = pQuery->m_ModifiedData.find(strDBName);
            if(itDB != pQuery->m_ModifiedData.end()) {
              itDB->second.insert(nsCAutoString(pArgA));
            } else {
              CDatabaseQuery::modifiedtables_t modifiedTables;
              modifiedTables.insert(nsCAutoString(pArgA));
              pQuery->m_ModifiedData.insert(
                std::make_pair<nsCString, CDatabaseQuery::modifiedtables_t>(
                strDBName, modifiedTables));
            }
          }
        }
      }
      break;

      case SQLITE_DROP_TEMP_INDEX:
      {
      }
      break;

      case SQLITE_DROP_TEMP_TABLE:
      {
        if(pArgA && strnicmp("sqlite_", pArgA, 7))
        {
          pQuery->m_HasChangedDataOfPersistQuery = PR_TRUE;

          {
            nsAutoLock lock(pQuery->m_pModifiedDataLock);
            nsCAutoString strDBName(pDBName);

            CDatabaseQuery::modifieddata_t::iterator itDB = pQuery->m_ModifiedData.find(strDBName);
            if(itDB != pQuery->m_ModifiedData.end()) {
              itDB->second.insert(nsCAutoString(pArgA));
            } else {
              CDatabaseQuery::modifiedtables_t modifiedTables;
              modifiedTables.insert(nsCAutoString(pArgA));
              pQuery->m_ModifiedData.insert(
                std::make_pair<nsCString, CDatabaseQuery::modifiedtables_t>(
                strDBName, modifiedTables));
            }
          }
        }
      }
      break;

      case SQLITE_DROP_TEMP_TRIGGER:
      {
      }
      break;

      case SQLITE_DROP_TEMP_VIEW:
      {
      }
      break;

      case SQLITE_DROP_TRIGGER:
      {
      }
      break;

      case SQLITE_DROP_VIEW:
      {
      }
      break;

      case SQLITE_INSERT:
      {
        if(pArgA && strnicmp("sqlite_", pArgA, 7))
        {
          pQuery->m_HasChangedDataOfPersistQuery = PR_TRUE;

          {
            nsAutoLock lock(pQuery->m_pModifiedDataLock);
            nsCAutoString strDBName(pDBName);

            if(strDBName.EqualsLiteral("main"))
            {
              nsAutoString strDBGUID;
              pQuery->GetDatabaseGUID(strDBGUID);
              strDBName = NS_ConvertUTF16toUTF8(strDBGUID);
            }

            CDatabaseQuery::modifieddata_t::iterator itDB = pQuery->m_ModifiedData.find(strDBName);
            if(itDB != pQuery->m_ModifiedData.end()) {
              itDB->second.insert(nsCAutoString(pArgA));
            } else {
              CDatabaseQuery::modifiedtables_t modifiedTables;
              modifiedTables.insert(nsCAutoString(pArgA));
              pQuery->m_ModifiedData.insert(
                std::make_pair<nsCString, CDatabaseQuery::modifiedtables_t>(
                strDBName, modifiedTables));
            }
          }
        }
      }
      break;

      case SQLITE_PRAGMA:
      {
      }
      break;

      case SQLITE_READ:
      {
        //If there's a table name but no column name, it means we're doing the initial SELECT on the table.
        //This is a good time to check if the query is supposed to be persistent and insert it
        //into the list of persistent queries.
        if(pQuery->m_PersistentQuery && strnicmp( "sqlite_", pArgA, 7 ) )
        {
          nsCOMPtr<sbIDatabaseEngine> p = do_GetService(SONGBIRD_DATABASEENGINE_CONTRACTID);
          if(p) p->AddPersistentQuery( pQuery, nsCAutoString(pArgA) );
        }
      }
      break;

      case SQLITE_SELECT:
      {
      }
      break;

      case SQLITE_TRANSACTION:
      {
      }
      break;

      case SQLITE_UPDATE:
      {
        if(pArgA && pArgB && pDBName)
        {
          pQuery->m_HasChangedDataOfPersistQuery = PR_TRUE;
          
          {
            nsAutoLock lock(pQuery->m_pModifiedDataLock);
            nsCAutoString strDBName(pDBName);

            if(strDBName.EqualsLiteral("main"))
            {
              nsAutoString strDBGUID;
              pQuery->GetDatabaseGUID(strDBGUID);
              strDBName = NS_ConvertUTF16toUTF8(strDBGUID);
            }

            CDatabaseQuery::modifieddata_t::iterator itDB = pQuery->m_ModifiedData.find(strDBName);
            if(itDB != pQuery->m_ModifiedData.end()) {
              itDB->second.insert(nsCAutoString(pArgA));
            } else {
              CDatabaseQuery::modifiedtables_t modifiedTables;
              modifiedTables.insert(nsCAutoString(pArgA));
              pQuery->m_ModifiedData.insert(
                std::make_pair<nsCString, CDatabaseQuery::modifiedtables_t>(
                strDBName, modifiedTables));
            }
          }
        }
      }
      break;

      case SQLITE_ATTACH:
      {
      }
      break;

      case SQLITE_DETACH:
      {
      }
      break;

    }
  }

  return ret;
} //SQLiteAuthorizer

NS_IMPL_THREADSAFE_ISUPPORTS2(CDatabaseEngine, sbIDatabaseEngine, nsIObserver)
NS_IMPL_THREADSAFE_ISUPPORTS1(QueryProcessorThread, nsIRunnable)

CDatabaseEngine *gEngine = nsnull;

// CLASSES ====================================================================
//-----------------------------------------------------------------------------
CDatabaseEngine::CDatabaseEngine()
: m_pDBStorePathLock(nsnull)
, m_pDatabasesGUIDListLock(nsnull)
, m_pDatabasesLock(nsnull)
, m_pDatabaseLocksLock(nsnull)
, m_pQueryProcessorMonitor(nsnull)
, m_QueryProcessorShouldShutdown(PR_FALSE)
, m_QueryProcessorQueueHasItem(PR_FALSE)
, m_pPersistentQueriesLock(nsnull)
, m_AttemptShutdownOnDestruction(PR_FALSE)
{
#ifdef PR_LOGGING
  gDatabaseEngineLog = PR_NewLogModule("DatabaseEngine");
#endif
} //ctor

//-----------------------------------------------------------------------------
/*virtual*/ CDatabaseEngine::~CDatabaseEngine()
{
  if (m_AttemptShutdownOnDestruction)
    Shutdown();

  if (m_pDatabasesLock)
    PR_DestroyLock(m_pDatabasesLock);
  if (m_pDatabaseLocksLock)
    PR_DestroyLock(m_pDatabaseLocksLock);
  if (m_pQueryProcessorMonitor)
    nsAutoMonitor::DestroyMonitor(m_pQueryProcessorMonitor);
  if (m_pPersistentQueriesLock)
    PR_DestroyLock(m_pPersistentQueriesLock);
  if (m_pDBStorePathLock)
    PR_DestroyLock(m_pDBStorePathLock);
} //dtor

//-----------------------------------------------------------------------------
CDatabaseEngine* CDatabaseEngine::GetSingleton()
{
  if (gEngine) {
    NS_ADDREF(gEngine);
    return gEngine;
  }

  NS_NEWXPCOM(gEngine, CDatabaseEngine);
  if (!gEngine)
    return nsnull;

  // AddRef once for us (released in nsModule destructor)
  NS_ADDREF(gEngine);

  // Set ourselves up properly
  if (NS_FAILED(gEngine->Init())) {
    NS_ERROR("Failed to Init CDatabaseEngine!");
    NS_RELEASE(gEngine);
    return nsnull;
  }

  // And AddRef once for the caller
  NS_ADDREF(gEngine);
  return gEngine;
}

NS_IMETHODIMP CDatabaseEngine::Init()
{
  m_pDatabasesLock = PR_NewLock();
  NS_ENSURE_TRUE(m_pDatabasesLock, NS_ERROR_OUT_OF_MEMORY);

  m_pDatabaseLocksLock = PR_NewLock();
  NS_ENSURE_TRUE(m_pDatabaseLocksLock, NS_ERROR_OUT_OF_MEMORY);

  m_pPersistentQueriesLock = PR_NewLock();
  NS_ENSURE_TRUE(m_pPersistentQueriesLock, NS_ERROR_OUT_OF_MEMORY);

  m_pDBStorePathLock = PR_NewLock();
  NS_ENSURE_TRUE(m_pDBStorePathLock, NS_ERROR_OUT_OF_MEMORY);

  m_pDatabasesGUIDListLock = PR_NewLock();
  NS_ENSURE_TRUE(m_pDatabasesGUIDListLock, NS_ERROR_OUT_OF_MEMORY);

  m_pQueryProcessorMonitor =
    nsAutoMonitor::NewMonitor("CDatabaseEngine.m_pQueryProcessorMonitor");
  NS_ENSURE_TRUE(m_pQueryProcessorMonitor, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv;
  for (PRInt32 i = 0; i < QUERY_PROCESSOR_THREAD_COUNT; i++) {
    nsCOMPtr<nsIThread> pThread;
    nsCOMPtr<nsIRunnable> pQueryProcessorRunner =
      new QueryProcessorThread(this);
    NS_ASSERTION(pQueryProcessorRunner, "Unable to create QueryProcessorRunner");
    if (!pQueryProcessorRunner)
      break;        
    rv = NS_NewThread(getter_AddRefs(pThread),
                      pQueryProcessorRunner);
    NS_ASSERTION(NS_SUCCEEDED(rv), "Unable to start thread");
    if (NS_SUCCEEDED(rv))
      m_QueryProcessorThreads.AppendObject(pThread);
  }

  rv = CreateDBStorePath();
  NS_ASSERTION(NS_SUCCEEDED(rv), "Unable to create db store folder in profile!");

  GenerateDBGUIDList();

  nsCOMPtr<nsIObserverService> observerService =
    do_GetService("@mozilla.org/observer-service;1", &rv);
  if(NS_SUCCEEDED(rv)) {
    rv = observerService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID,
                                      PR_FALSE);
  }

  // This shouldn't be an 'else' case because we want to set this flag if
  // either of the above calls failed
  if (NS_FAILED(rv)) {
    NS_ERROR("Unable to register xpcom-shutdown observer");
    m_AttemptShutdownOnDestruction = PR_TRUE;
  }

  return NS_OK;
}

NS_IMETHODIMP CDatabaseEngine::Shutdown()
{
  PRInt32 count = m_QueryProcessorThreads.Count();
  if (count) {
    {
      nsAutoMonitor mon(m_pQueryProcessorMonitor);
      m_QueryProcessorShouldShutdown = PR_TRUE;
      mon.NotifyAll();
    }

    for (PRInt32 i = 0; i < count; i++)
      m_QueryProcessorThreads[i]->Shutdown();
  }

  NS_WARN_IF_FALSE(NS_SUCCEEDED(CloseAllDB()), "CloseAllDB Failed!");
  NS_WARN_IF_FALSE(NS_SUCCEEDED(ClearAllDBLocks()), "ClearAllDBLocks Failed!");

  m_DatabaseLocks.clear();
  m_Databases.clear();

  NS_WARN_IF_FALSE(NS_SUCCEEDED(ClearPersistentQueries()),
                   "ClearPersistentQueries Failed!");
  NS_WARN_IF_FALSE(NS_SUCCEEDED(ClearQueryQueue()), "ClearQueryQueue Failed!");

  return NS_OK;
}

NS_IMETHODIMP CDatabaseEngine::Observe(nsISupports *aSubject,
                                       const char *aTopic,
                                       const PRUnichar *aData)
{
  // Bail if we don't care about the message
  if (strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID))
    return NS_OK;
  
  // Shutdown our threads
  nsresult rv = Shutdown();
  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Shutdown Failed!");

  // And remove ourselves from the observer service
  nsCOMPtr<nsIObserverService> observerService =
    do_GetService("@mozilla.org/observer-service;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return observerService->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
}

//-----------------------------------------------------------------------------
PRInt32 CDatabaseEngine::OpenDB(const nsAString &dbGUID)
{
  nsAutoLock lock(m_pDatabasesLock);

  sqlite3 *pDB = nsnull;

  nsAutoString strFilename;
  GetDBStorePath(dbGUID, strFilename);
  
  // Kick sqlite in the pants
  PRInt32 ret = sqlite3_open16(PromiseFlatString(strFilename).get(), &pDB);

  // Remember what we just loaded
  if(ret == SQLITE_OK)
  {
    m_Databases.insert(std::make_pair(PromiseFlatString(dbGUID), pDB));

    {
      nsAutoLock lock(m_pDatabasesGUIDListLock);
      PRBool found = PR_FALSE;
      PRInt32 size = m_DatabasesGUIDList.size(), current = 0;
      for(; current < size; current++) {
        if(m_DatabasesGUIDList[current] == dbGUID)
          found = PR_TRUE;
      }

      if(!found)
        m_DatabasesGUIDList.push_back(PromiseFlatString(dbGUID));
    }

#if defined(USE_SQLITE_FULL_DISK_CACHING)
    sqlite3_busy_timeout(pDB, 60000);

    char *strErr = nsnull;
    sqlite3_exec(pDB, "PRAGMA synchronous = 0", nsnull, nsnull, &strErr);
    if(strErr) sqlite3_free(strErr);
#endif

    NS_ASSERTION( ret == SQLITE_OK, "CDatabaseEngine: Couldn't create/open database!");
  }

  return ret;
} //OpenDB

//-----------------------------------------------------------------------------
PRInt32 CDatabaseEngine::CloseDB(const nsAString &dbGUID)
{
  PRInt32 ret = 0;
  nsAutoLock lock(m_pDatabasesLock);

  databasemap_t::iterator itDatabases = m_Databases.find(PromiseFlatString(dbGUID));

  if(itDatabases != m_Databases.end())
  {
    sqlite3_interrupt(itDatabases->second);
    ret = sqlite3_close(itDatabases->second);
    m_Databases.erase(itDatabases);
  }
 
  return ret;
} //CloseDB

//-----------------------------------------------------------------------------
PRInt32 CDatabaseEngine::DropDB(const nsAString &dbGUID)
{
  return 0;
} //DropDB

//-----------------------------------------------------------------------------
/* [noscript] PRInt32 SubmitQuery (in CDatabaseQueryPtr dbQuery); */
NS_IMETHODIMP CDatabaseEngine::SubmitQuery(CDatabaseQuery * dbQuery, PRInt32 *_retval)
{
  *_retval = SubmitQueryPrivate(dbQuery);
  return NS_OK;
}

//-----------------------------------------------------------------------------
PRInt32 CDatabaseEngine::SubmitQueryPrivate(CDatabaseQuery *dbQuery)
{
  PRInt32 ret = 0;

  if(!dbQuery) return 1;

  // If the query is already executing, do not add it.  This is to prevent
  // the same query from getting executed simultaneously
  PRBool isExecuting;
  dbQuery->IsExecuting(&isExecuting);
  if(isExecuting) {
    return 0;
  }

  {
    nsAutoMonitor mon(m_pQueryProcessorMonitor);

    dbQuery->AddRef();
    dbQuery->m_IsExecuting = PR_TRUE;

    m_QueryQueue.push_back(dbQuery);
    m_QueryProcessorQueueHasItem = PR_TRUE;
    mon.Notify();
  }

  PRBool bAsyncQuery = PR_FALSE;
  dbQuery->IsAyncQuery(&bAsyncQuery);

  if(!bAsyncQuery)
  {
    dbQuery->WaitForCompletion(&ret);
    dbQuery->GetLastError(&ret);
  }

  return ret;
} //SubmitQuery

//-----------------------------------------------------------------------------
/* [noscript] void AddPersistentQuery (in CDatabaseQueryPtr dbQuery, in stlCStringRef strTableName); */
NS_IMETHODIMP CDatabaseEngine::AddPersistentQuery(CDatabaseQuery * dbQuery, const nsACString & strTableName)
{
  AddPersistentQueryPrivate(dbQuery, strTableName);
  return NS_OK;
}

//-----------------------------------------------------------------------------
void CDatabaseEngine::AddPersistentQueryPrivate(CDatabaseQuery *pQuery, const nsACString &strTableName)
{
  nsAutoLock lock(m_pPersistentQueriesLock);

  nsAutoString strDBGUID;
  pQuery->GetDatabaseGUID(strDBGUID);

  NS_ConvertUTF16toUTF8 strTheDBGUID(strDBGUID);

  NS_ASSERTION(strTheDBGUID.IsEmpty() != PR_TRUE, "No DB GUID present in Query that requested persistent execution!");

  querypersistmap_t::iterator itPersistentQueries = m_PersistentQueries.find(strTheDBGUID);
  if(itPersistentQueries != m_PersistentQueries.end())
  {
    tablepersistmap_t::iterator itTableQuery = itPersistentQueries->second.find(PromiseFlatCString(strTableName));
    if(itTableQuery != itPersistentQueries->second.end())
    {
      querylist_t::iterator itQueries = itTableQuery->second.begin();
      for( ; itQueries != itTableQuery->second.end(); itQueries++)
      {
        if((*itQueries) == pQuery)
          return;
      }

      NS_IF_ADDREF(pQuery);
      itTableQuery->second.insert(itTableQuery->second.end(), pQuery);
    }
    else
    {
      NS_IF_ADDREF(pQuery);

      querylist_t queryList;
      queryList.push_back(pQuery);

      itPersistentQueries->second.insert(std::make_pair<nsCString, querylist_t>(PromiseFlatCString(strTableName), queryList));
    }
  }
  else
  {
    NS_IF_ADDREF(pQuery);

    querylist_t queryList;
    queryList.push_back(pQuery);

    tablepersistmap_t tableMap;
    tableMap.insert(std::make_pair<nsCString, querylist_t>(PromiseFlatCString(strTableName), queryList));

    m_PersistentQueries.insert(std::make_pair<nsCString, tablepersistmap_t>(strTheDBGUID, tableMap));
  }

  {
    PR_Lock(pQuery->m_pPersistentQueryTableLock);
    pQuery->m_PersistentQueryTable = strTableName;
    pQuery->m_IsPersistentQueryRegistered = PR_TRUE;
    PR_Unlock(pQuery->m_pPersistentQueryTableLock);
  }

} //AddPersistentQuery

//-----------------------------------------------------------------------------
/* [noscript] void RemovePersistentQuery (in CDatabaseQueryPtr dbQuery); */
NS_IMETHODIMP CDatabaseEngine::RemovePersistentQuery(CDatabaseQuery * dbQuery)
{
  RemovePersistentQueryPrivate(dbQuery);
  return NS_OK;
}

//-----------------------------------------------------------------------------
void CDatabaseEngine::RemovePersistentQueryPrivate(CDatabaseQuery *pQuery)
{
  if(!pQuery->m_IsPersistentQueryRegistered)
    return;

  nsAutoLock lock(m_pPersistentQueriesLock);

  nsCAutoString tableName;
  nsAutoString strDBGUID;

  pQuery->GetDatabaseGUID(strDBGUID);
  NS_ConvertUTF16toUTF8 strTheDBGUID(strDBGUID);

  {
    PR_Lock(pQuery->m_pPersistentQueryTableLock);
    tableName = pQuery->m_PersistentQueryTable;
    PR_Unlock(pQuery->m_pPersistentQueryTableLock);
  }

  querypersistmap_t::iterator itPersistentQueries = m_PersistentQueries.find(strTheDBGUID);
  if(itPersistentQueries != m_PersistentQueries.end())
  {
    tablepersistmap_t::iterator itTableQuery = itPersistentQueries->second.find(tableName);
    if(itTableQuery != itPersistentQueries->second.end())
    {
      querylist_t::iterator itQueries = itTableQuery->second.begin();
      for( ; itQueries != itTableQuery->second.end(); itQueries++)
      {
        if((*itQueries) == pQuery)
        {
          NS_RELEASE((*itQueries));
          itTableQuery->second.erase(itQueries);

          return;
        }
      }
    }
  }

  return;
} //RemovePersistentQuery

//-----------------------------------------------------------------------------
nsresult CDatabaseEngine::LockDatabase(sqlite3 *pDB)
{
  NS_ENSURE_ARG_POINTER(pDB);
  PRMonitor* lock = nsnull;
  {
    nsAutoLock dbLock(m_pDatabaseLocksLock);
    databaselockmap_t::iterator itLock = m_DatabaseLocks.find(pDB);
    if(itLock != m_DatabaseLocks.end()) {
      lock = itLock->second;
      NS_ENSURE_TRUE(lock, NS_ERROR_NULL_POINTER);
    }
    else {
      lock = PR_NewMonitor();
      NS_ENSURE_TRUE(lock, NS_ERROR_OUT_OF_MEMORY);
      m_DatabaseLocks.insert(std::make_pair<sqlite3*, PRMonitor*>(pDB, lock));
    }
  }

  PR_EnterMonitor(lock);
  return NS_OK;
} //LockDatabase

//-----------------------------------------------------------------------------
nsresult CDatabaseEngine::UnlockDatabase(sqlite3 *pDB)
{
  NS_ENSURE_ARG_POINTER(pDB);
  PRMonitor* lock = nsnull;
  {
    nsAutoLock dbLock(m_pDatabaseLocksLock);
    databaselockmap_t::iterator itLock = m_DatabaseLocks.find(pDB);
    NS_ASSERTION(itLock != m_DatabaseLocks.end(), "Called UnlockDatabase on a database that isn't in our map");
    lock = itLock->second;
  }
  NS_ASSERTION(lock, "Null lock stored in database map");
  PR_ExitMonitor(lock);
  return NS_OK;
} //UnlockDatabase

//-----------------------------------------------------------------------------
nsresult CDatabaseEngine::ClearAllDBLocks()
{
  nsAutoLock dbLock(m_pDatabaseLocksLock);

  databaselockmap_t::iterator itLock = m_DatabaseLocks.begin();
  databaselockmap_t::iterator itEnd = m_DatabaseLocks.end();

  for(PRMonitor* lock = nsnull; itLock != itEnd; itLock++)
  {
    lock = itLock->second;
    if (lock) {
      PR_DestroyMonitor(lock);
      lock = nsnull;
    }
  }
  m_DatabaseLocks.clear();
  return NS_OK;
} //ClearAllDBLocks

//-----------------------------------------------------------------------------
nsresult CDatabaseEngine::CloseAllDB()
{
  PRInt32 ret = nsnull;
  nsAutoLock lock(m_pDatabasesLock);

  databasemap_t::iterator itDatabases = m_Databases.begin();

  if(itDatabases != m_Databases.end())
  {
    sqlite3_interrupt(itDatabases->second);
    while((ret = sqlite3_close(itDatabases->second)) != SQLITE_OK)
      PR_Sleep(33);

    m_Databases.erase(itDatabases);
  }

  return NS_OK;
} //CloseAllDB

//-----------------------------------------------------------------------------
nsresult CDatabaseEngine::ClearPersistentQueries()
{
  querypersistmap_t::iterator itPersistentQueries = m_PersistentQueries.begin();
  for(; itPersistentQueries != m_PersistentQueries.end(); itPersistentQueries++)
  {
    tablepersistmap_t::iterator itTableQuery = itPersistentQueries->second.begin();
    for(; itTableQuery != itPersistentQueries->second.end(); itTableQuery++)
    {
      querylist_t::iterator itQueries = itTableQuery->second.begin();
      for( ; itQueries != itTableQuery->second.end(); itQueries++)
      {
        NS_IF_RELEASE((*itQueries));
      }
    }
  }

  m_PersistentQueries.clear();

  return NS_OK;
}

//-----------------------------------------------------------------------------
nsresult CDatabaseEngine::ClearQueryQueue()
{
  while(!m_QueryQueue.empty()) {
    CDatabaseQuery *pQuery = m_QueryQueue.front();
    m_QueryQueue.pop_front();

    NS_IF_RELEASE(pQuery);
  }
  
  return NS_OK;
}

//-----------------------------------------------------------------------------
sqlite3 *CDatabaseEngine::GetDBByGUID(const nsAString &dbGUID, PRBool bCreateIfNotOpen /*= PR_FALSE*/)
{
  sqlite3 *pRet = nsnull;

  pRet = FindDBByGUID(dbGUID);

  if(pRet == nsnull)
  {
    int ret = OpenDB(dbGUID);
    if(ret == SQLITE_OK)
    {
      pRet = FindDBByGUID(dbGUID);
    }
  }

  return pRet;
} //GetDBByGUID


//-----------------------------------------------------------------------------
void CDatabaseEngine::GenerateDBGUIDList()
{
  PRInt32 nRet = 0;
  PRBool bFlag = PR_FALSE;
  nsresult rv;

  nsCOMPtr<nsILocalFile> pDBDirectory;
  nsCOMPtr<nsIServiceManager> svcMgr;
  rv = NS_GetServiceManager(getter_AddRefs(svcMgr));

  if(NS_FAILED(rv)) return;

  PR_Lock(m_pDBStorePathLock);
  rv = NS_NewLocalFile(m_DBStorePath, PR_FALSE, getter_AddRefs(pDBDirectory));
  PR_Unlock(m_pDBStorePathLock);

  if (NS_FAILED(rv)) return;

  rv = pDBDirectory->IsDirectory(&bFlag);
  if(NS_FAILED(rv)) return;

  if(bFlag)
  {
    nsCOMPtr<nsISimpleEnumerator> pDirEntries;
    rv = pDBDirectory->GetDirectoryEntries(getter_AddRefs(pDirEntries));

    if(NS_SUCCEEDED(rv))
    {
      PRBool bHasMore;

      for(;;)
      {
        if(pDirEntries &&
           NS_SUCCEEDED(pDirEntries->HasMoreElements(&bHasMore)) &&
           bHasMore)
        {

          nsCOMPtr<nsISupports> pDirEntry;
          rv = pDirEntries->GetNext(getter_AddRefs(pDirEntry));

          if(NS_SUCCEEDED(rv))
          {
            nsCOMPtr<nsIFile> pEntry = do_QueryInterface(pDirEntry, &rv);
            if(NS_SUCCEEDED(rv))
            {
              PRBool bIsFile;

              if(NS_SUCCEEDED(pEntry->IsFile(&bIsFile)) && bIsFile)
              {
                nsAutoString strLeaf;
                rv = pEntry->GetLeafName(strLeaf);
                if (NS_SUCCEEDED(rv))
                {
                  NS_NAMED_LITERAL_STRING(dbExt, ".db");
                  PRInt32 index = strLeaf.Find(dbExt);
                  if (index > -1)
                  {
                    strLeaf.Cut(index, dbExt.Length());

                    PR_Lock(m_pDatabasesGUIDListLock);
                    m_DatabasesGUIDList.push_back(strLeaf);
                    PR_Unlock(m_pDatabasesGUIDListLock);

                    ++nRet;
                  }
                }
              }
            }
          }
        }
        else
        {
          break;
        }
      }
    }
  }

  return;
}

//-----------------------------------------------------------------------------
PRInt32 CDatabaseEngine::GetDBGUIDList(std::vector<nsString> &vGUIDList)
{
  PR_Lock(m_pDatabasesGUIDListLock);
  vGUIDList = m_DatabasesGUIDList;
  PR_Unlock(m_pDatabasesGUIDListLock);
  return vGUIDList.size();
} //GetDBGUIDList

//-----------------------------------------------------------------------------
sqlite3 *CDatabaseEngine::FindDBByGUID(const nsAString &dbGUID)
{
  sqlite3 *pRet = nsnull;

  PR_Lock(m_pDatabasesLock);

  databasemap_t::const_iterator itDatabases = m_Databases.find(PromiseFlatString(dbGUID));
  if(itDatabases != m_Databases.end())
  {
    pRet = itDatabases->second;
  }

  PR_Unlock(m_pDatabasesLock);

  return pRet;
} //FindDBByGUID

//-----------------------------------------------------------------------------
/*static*/ void PR_CALLBACK CDatabaseEngine::QueryProcessor(CDatabaseEngine* pEngine)
{
  NS_NAMED_LITERAL_STRING(allToken, "*");
  NS_NAMED_LITERAL_STRING(strAttachToken, "ATTACH DATABASE \"");
  NS_NAMED_LITERAL_STRING(strStartToken, "AS \"");
  NS_NAMED_LITERAL_STRING(strEndToken, "\"");

  CDatabaseQuery *pQuery = nsnull;

  while(PR_TRUE)
  {
    pQuery = nsnull;

    { // Enter Monitor
      nsAutoMonitor mon(pEngine->m_pQueryProcessorMonitor);
      while (!pEngine->m_QueryProcessorQueueHasItem && !pEngine->m_QueryProcessorShouldShutdown)
        mon.Wait();

      // Handle shutdown request
      if (pEngine->m_QueryProcessorShouldShutdown) {
#if defined(XP_WIN)
        sqlite3_thread_cleanup();
#endif
        return;
      }

      // We must have an item in the queue
      pQuery = pEngine->m_QueryQueue.front();
      pEngine->m_QueryQueue.pop_front();
      if (pEngine->m_QueryQueue.empty()) {
        pEngine->m_QueryProcessorQueueHasItem = PR_FALSE;
      }
    } // Exit Monitor

    NS_ASSERTION(pQuery, "How can we get here without a query?");

    PRBool bAllDB = PR_FALSE;
    sqlite3 *pDB = nsnull;
    sqlite3 *pSecondDB = nsnull;
    nsAutoString dbGUID;
    pQuery->GetDatabaseGUID(dbGUID);

    PR_LOG(gDatabaseEngineLog, PR_LOG_WARNING,
           ("DBE: Process Start, thread 0x%x query 0x%x",
           PR_GetCurrentThread(), pQuery));

    nsAutoString lowercaseGUID(dbGUID);
    {
      ToLowerCase(lowercaseGUID);
      bAllDB = allToken.Equals(lowercaseGUID);
    }

    if(!bAllDB)
      pDB = pEngine->GetDBByGUID(dbGUID, PR_TRUE);

    if(!pDB && !bAllDB)
    {
      //Some bad error we'll talk about later.
      pQuery->SetLastError(SQLITE_ERROR);
    }
    else
    {
      std::vector<nsString> vDBList;
      PRInt32 nNumDB = 1;
      PRInt32 nQueryCount = 0;
      PRBool bFirstRow = PR_TRUE;

      //Default return error.
      pQuery->SetLastError(SQLITE_ERROR);
      pQuery->GetQueryCount(&nQueryCount);

      if(bAllDB)
        nNumDB = pEngine->GetDBGUIDList(vDBList);

      for(PRInt32 i = 0; i < nQueryCount && !pQuery->m_IsAborting; i++)
      {

        int retDB = 0;
        sqlite3_stmt *pStmt = nsnull;

        nsAutoString strQuery;
        bindParameterArray_t* pParameters;
        const void *pzTail = nsnull;

        pQuery->GetQuery(i, strQuery);
        pParameters = pQuery->GetQueryParameters(i);

        PRInt32 attachOffset = strQuery.Find(strAttachToken);
        if(attachOffset > -1)
        {
          PRInt32 startOffset = strQuery.Find(strStartToken, attachOffset);
          if(startOffset > -1)
          {
            PRInt32 endOffset = strQuery.Find(strEndToken, startOffset);
            
            if(pSecondDB) {
              pEngine->UnlockDatabase(pSecondDB);
              pSecondDB = nsnull;
            }

            nsAutoString strSecondDBGUID(Substring(strQuery,
                                                   (PRUint32)startOffset,
                                                   (PRUint32)(endOffset - startOffset)));

            pSecondDB = pEngine->GetDBByGUID(strSecondDBGUID, PR_TRUE);
            if(pSecondDB)
            {
              nsAutoString strDBPath(strSecondDBGUID);
              strDBPath.AppendLiteral(".db");
              
              PRInt32 index = strQuery.Find(strDBPath);
              if(index > -1)
              {
                pEngine->GetDBStorePath(strSecondDBGUID, strDBPath);
                strQuery.Replace(index, strDBPath.Length(), strDBPath);
              }

              pEngine->LockDatabase(pSecondDB);
            }
          }
        }

        for(PRInt32 j = 0; j < nNumDB; j++)
        {
          nsAutoString dbName;
          if(!bAllDB)
          {
            pQuery->GetDatabaseGUID(dbName);
            pEngine->LockDatabase(pDB);
            retDB = sqlite3_set_authorizer(pDB, SQLiteAuthorizer, pQuery);

#if defined(HARD_SANITY_CHECK)
            if(retDB != SQLITE_OK)
            {
              const char *szErr = sqlite3_errmsg(pDB);
              nsCAutoString log;
              log.Append(szErr);
              log.AppendLiteral("\n");
              NS_WARNING(log.get());
            }
#endif
          }
          else
          {
            dbName.Assign(vDBList[j]);
            pDB = pEngine->GetDBByGUID(vDBList[j], PR_TRUE);
            
            //Just in case there was some kind of bad error while attempting to get the database or create it.
            if ( !pDB ) 
              continue;
            
            pEngine->LockDatabase(pDB);
            retDB = sqlite3_set_authorizer(pDB, SQLiteAuthorizer, pQuery);

#if defined(HARD_SANITY_CHECK)
            if(retDB != SQLITE_OK)
            {
              const char *szErr = sqlite3_errmsg(pDB);
              nsCAutoString log;
              log.Append(szErr);
              log.AppendLiteral("\n");
              NS_WARNING(log.get());
            }
#endif
          }

          retDB = sqlite3_prepare16(pDB, PromiseFlatString(strQuery).get(), (int)strQuery.Length() * sizeof(PRUnichar), &pStmt, &pzTail);

          // If we have parameters for this query, bind them
          if(retDB == SQLITE_OK) {

            PRUint32 len = pParameters->Length();
            for(PRUint32 i = 0; i < len; i++) {
              CQueryParameter& p = pParameters->ElementAt(i);

              switch(p.type) {
                case ISNULL:
                  sqlite3_bind_null(pStmt, i + 1);
                  break;
                case UTF8STRING:
                  sqlite3_bind_text(pStmt, i + 1,
                                    p.utf8StringValue.get(),
                                    p.utf8StringValue.Length(),
                                    SQLITE_TRANSIENT);
                  break;
                case STRING:
                  sqlite3_bind_text16(pStmt, i + 1,
                                      p.stringValue.get(),
                                      p.stringValue.Length() * 2,
                                      SQLITE_TRANSIENT);
                  break;
                case DOUBLE:
                  sqlite3_bind_double(pStmt, i + 1, p.doubleValue);
                  break;
                case INT32:
                  sqlite3_bind_int(pStmt, i + 1, p.int32Value);
                  break;
                case INT64:
                  sqlite3_bind_int64(pStmt, i + 1, p.int64Value);
                  break;
              }
            }
          }

          PR_LOG(gDatabaseEngineLog, PR_LOG_WARNING,
                 ("DBE: '%s' on '%s'\n",
                 NS_ConvertUTF16toUTF8(dbName).get(),
                 NS_ConvertUTF16toUTF8(strQuery).get()));
          pQuery->m_CurrentQuery = i;

          if(retDB != SQLITE_OK)
          {
            //Some bad error we'll talk about later.
            pQuery->SetLastError(SQLITE_ERROR);

#if defined(HARD_SANITY_CHECK)
            if(retDB != SQLITE_OK)
            {
              const char *szErr = sqlite3_errmsg(pDB);
              nsCAutoString log;
              log.Append(szErr);
              log.AppendLiteral("\n");
              //NS_WARNING(log.get());
            }
#endif
          }
          else
          {
            PRInt32 nRetryCount = 0;
            PRInt32 totalRows = 0;

            do
            {
              retDB = sqlite3_step(pStmt);

              switch(retDB)
              {
                case SQLITE_ROW: 
                {
                  CDatabaseResult *pRes = pQuery->GetResultObject();
                  PR_Lock(pQuery->m_pQueryResultLock);

                  int nCount = sqlite3_column_count(pStmt);

                  if(bFirstRow)
                  {
                    bFirstRow = PR_FALSE;

                    PR_Unlock(pQuery->m_pQueryResultLock);
                    pRes->ClearResultSet();
                    PR_Lock(pQuery->m_pQueryResultLock);

                    std::vector<nsString> vColumnNames;
                    vColumnNames.reserve(nCount);

                    for(int i = 0; i < nCount; i++)
                    {
                      PRUnichar *p = (PRUnichar *)sqlite3_column_name16(pStmt, i);
                      nsString strColumnName;
                      
                      if(p)
                        strColumnName = p;
                      
                      vColumnNames.push_back(strColumnName);
                    }

                    pRes->SetColumnNames(vColumnNames);
                  }

                  std::vector<nsString> vCellValues;
                  vCellValues.reserve(nCount);

                  PR_LOG(gDatabaseEngineLog, PR_LOG_DEBUG,
                         ("DBE: Result row %d:",
                         totalRows));

                  for(int i = 0; i < nCount; i++)
                  {
                    PRUnichar *p = (PRUnichar *)sqlite3_column_text16(pStmt, i);
                    nsString strCellValue;

                    if(p)
                      strCellValue = p;

                    vCellValues.push_back(strCellValue);
                    PR_LOG(gDatabaseEngineLog, PR_LOG_DEBUG,
                           ("Column %d: '%s' ", i,
                           NS_ConvertUTF16toUTF8(strCellValue).get()));
                  }

                  pRes->AddRow(vCellValues);
                  PR_Unlock(pQuery->m_pQueryResultLock);
                }
                totalRows++;
                break;

                case SQLITE_DONE:
                {
                  pQuery->SetLastError(SQLITE_OK);
                  PR_LOG(gDatabaseEngineLog, PR_LOG_DEBUG,
                         ("Query complete, %d rows", totalRows));
                }
                break;

                case SQLITE_MISUSE:
                {
#if defined(HARD_SANITY_CHECK)
#if defined(XP_WIN)
                  OutputDebugStringA("SQLITE: MISUSE\n");
                  OutputDebugStringW(PromiseFlatString(strQuery).get());
                  OutputDebugStringW(L"\n");

                  const char *szErr = sqlite3_errmsg(pDB);
                  OutputDebugStringA(szErr);
                  OutputDebugStringA("\n");
#endif

                  {
                    const char *szErr = sqlite3_errmsg(pDB);
                    nsCAutoString log;
                    log.Append(szErr);
                    log.AppendLiteral("\n");
                    NS_WARNING(log.get());
                  }
#endif
                }
                break;

                case SQLITE_BUSY:
                {
#if defined(HARD_SANITY_CHECK)
#if defined(XP_WIN)
                  OutputDebugStringA("SQLITE: BUSY\n");
                  OutputDebugStringW(PromiseFlatString(strQuery).get());
                  OutputDebugStringW(L"\n");

                  const char *szErr = sqlite3_errmsg(pDB);
                  OutputDebugStringA(szErr);
                  OutputDebugStringA("\n");
#endif
#endif
                  sqlite3_reset(pStmt);

                  if(nRetryCount++ > SQLITE_MAX_RETRIES)
                  {
                    PR_Sleep(PR_MillisecondsToInterval(250));
                    retDB = SQLITE_ROW;
                  }
                }
                break;

                default:
                {
                  pQuery->SetLastError(retDB);

#if defined(HARD_SANITY_CHECK)
#if defined(XP_WIN)
                  OutputDebugStringA("SQLITE: DEFAULT ERROR\n");
                  OutputDebugStringA("Error Code: ");
                  char szCode[64] = {0};
                  OutputDebugStringA(itoa(retDB, szCode, 10));
                  OutputDebugStringA("\n");

                  OutputDebugStringW(PromiseFlatString(strQuery).get());
                  OutputDebugStringW(L"\n");

                  const char *szErr = sqlite3_errmsg(pDB);
                  OutputDebugStringA(szErr);
                  OutputDebugStringA("\n");
#endif

                  {
                    const char *szErr = sqlite3_errmsg(pDB);
                    nsCAutoString log;
                    log.Append(szErr);
                    log.AppendLiteral("\n");
                    NS_WARNING(log.get());
                  }
#endif
                }
              }
            }
            while(retDB == SQLITE_ROW);
          }

          //Didn't get any rows
          if(bFirstRow)
          {
            PRBool isPersistent = PR_FALSE;
            pQuery->IsPersistentQuery(&isPersistent);

            if(isPersistent)
            {
              nsAutoString strTableName(strQuery);
              ToLowerCase(strTableName);

              NS_NAMED_LITERAL_STRING(searchStr, " from ");

              PRInt32 foundIndex = strTableName.Find(searchStr);
              if(foundIndex > -1)
              {
                PRUint32 nCutLen = foundIndex + searchStr.Length();
                strTableName.Cut(0, nCutLen);

                PRUint32 offset = 0;
                while(strTableName.CharAt(offset) == NS_L(' '))
                  offset++;

                strTableName.Cut(0, offset);

                NS_NAMED_LITERAL_STRING(spaceStr, " ");
                foundIndex = strTableName.Find(spaceStr);

                if(foundIndex > -1)
                  strTableName.SetLength((PRUint32)foundIndex);

                // XXXben This makes an nsCAutoString... Is that ok? Otherwise need a new CString
                pEngine->AddPersistentQuery(pQuery, NS_ConvertUTF16toUTF8(strTableName));
              }
            }
            CDatabaseResult *pRes = pQuery->GetResultObject();
            pRes->ClearResultSet();
          }

          //Always release the statement.
          retDB = sqlite3_finalize(pStmt);

          pEngine->UnlockDatabase(pDB);

#if defined(HARD_SANITY_CHECK)
          if(retDB != SQLITE_OK)
          {
            const char *szErr = sqlite3_errmsg(pDB);
            nsCAutoString log;
            log.Append(szErr);
            log.AppendLiteral("\n");
            NS_WARNING(log.get());
          }
#endif
        }

        delete pParameters;
      }
    }

    if(pSecondDB != nsnull)
      pEngine->UnlockDatabase(pSecondDB);

    pQuery->m_IsExecuting = PR_FALSE;
    pQuery->m_IsAborting = PR_FALSE;

    PR_LOG(gDatabaseEngineLog, PR_LOG_WARNING,
           ("DBE: Process End"));

    //Whatever happened, the query is done running now.
    {
      nsAutoMonitor mon(pQuery->m_pQueryRunningMonitor);
      pQuery->m_QueryHasCompleted = PR_TRUE;
      mon.NotifyAll();
    }

    //Check if this query changed any data 
    pEngine->UpdatePersistentQueries(pQuery);

    //Check if this query is a persistent query so we can now fire off the callback.
    pEngine->DoPersistentCallback(pQuery);

    NS_IF_RELEASE(pQuery);
  } // while
} //QueryProcessor

//-----------------------------------------------------------------------------
void CDatabaseEngine::UpdatePersistentQueries(CDatabaseQuery *pQuery)
{
  NS_NAMED_LITERAL_STRING(strAllToken, "*");
  NS_NAMED_LITERAL_CSTRING(cstrAllToken, "*");

  if(pQuery->m_HasChangedDataOfPersistQuery && 
     !pQuery->m_PersistentQuery)
  {
    nsAutoLock lock(pQuery->m_pModifiedDataLock);
    CDatabaseQuery::modifieddata_t::const_iterator itDB = pQuery->m_ModifiedData.begin();

    for(; itDB != pQuery->m_ModifiedData.end(); itDB++)
    {
      nsAutoLock pqLock(m_pPersistentQueriesLock);
      querypersistmap_t::iterator itPersistentQueries = m_PersistentQueries.find(itDB->first);

      if(itPersistentQueries != m_PersistentQueries.end())
      {
        nsCAutoString strTableName;
        CDatabaseQuery::modifiedtables_t::const_iterator i = itDB->second.begin();
        for (; i != itDB->second.end(); ++i )
        {
          tablepersistmap_t::iterator itTableQuery = itPersistentQueries->second.find((*i));
          if(itTableQuery != itPersistentQueries->second.end())
          {
            querylist_t::iterator itQueries = itTableQuery->second.begin();
            for(; itQueries != itTableQuery->second.end(); itQueries++)
            {
              SubmitQueryPrivate((*itQueries));
            }
          }
        }
      }

      itPersistentQueries = m_PersistentQueries.find(cstrAllToken);
      if(itPersistentQueries != m_PersistentQueries.end())
      {
        CDatabaseQuery::modifiedtables_t::const_iterator i = itDB->second.begin();
        for (; i != itDB->second.end(); ++i )
        {
          tablepersistmap_t::iterator itTableQuery = itPersistentQueries->second.find((*i));
          if(itTableQuery != itPersistentQueries->second.end())
          {
            querylist_t::iterator itQueries = itTableQuery->second.begin();
            for(; itQueries != itTableQuery->second.end(); itQueries++)
            {
              (*itQueries)->SetDatabaseGUID(strAllToken);
              SubmitQueryPrivate((*itQueries));
            }
          }
        }
      }
    }
  }

  {
    PR_Lock(pQuery->m_pModifiedDataLock);
    pQuery->m_HasChangedDataOfPersistQuery = PR_FALSE;
    pQuery->m_ModifiedData.clear();
    PR_Unlock(pQuery->m_pModifiedDataLock);
  }

} //UpdatePersistentQueries

//-----------------------------------------------------------------------------
void CDatabaseEngine::DoPersistentCallback(CDatabaseQuery *pQuery)
{
  if(pQuery->m_PersistentQuery)
  {
    nsCOMPtr<sbIDatabaseResult> pDBResult;
    nsAutoString strGUID, strQuery;

    pQuery->GetResultObject(getter_AddRefs(pDBResult));
    pQuery->GetDatabaseGUID(strGUID);
    pQuery->GetQuery(0, strQuery);

    nsAutoLock lock(pQuery->m_pPersistentCallbackListLock);
    CDatabaseQuery::persistentcallbacklist_t::const_iterator itCallback = pQuery->m_PersistentCallbackList.begin();
    CDatabaseQuery::persistentcallbacklist_t::const_iterator itEnd = pQuery->m_PersistentCallbackList.end();

    for(; itCallback != itEnd; itCallback++)
    {
      if((*itCallback))
      {
        try
        {
          (*itCallback)->OnQueryEnd(pDBResult, strGUID, strQuery);
        }
        catch(...)
        {
          //bad monkey!
        }
      }
    }
  }
} //DoPersistentCallback

//-----------------------------------------------------------------------------
nsresult CDatabaseEngine::CreateDBStorePath()
{
  nsresult rv = NS_ERROR_FAILURE;
  nsAutoLock lock(m_pDBStorePathLock);

  nsCOMPtr<nsIFile> f;

  rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(f));
  if(NS_FAILED(rv)) return rv;

  rv = f->Append(NS_LITERAL_STRING("db"));
  if(NS_FAILED(rv)) return rv;

  PRBool dirExists = PR_FALSE; 
  rv = f->Exists(&dirExists);
  if(NS_FAILED(rv)) return rv;

  if(!dirExists) 
  {
    rv = f->Create(nsIFile::DIRECTORY_TYPE, 0700);
    if(NS_FAILED(rv)) return rv;
  }

  rv = f->GetPath(m_DBStorePath);
  if(NS_FAILED(rv)) return rv;

  return NS_OK;
}

//-----------------------------------------------------------------------------
nsresult CDatabaseEngine::GetDBStorePath(const nsAString &dbGUID, nsAString &strPath)
{
  nsresult rv = NS_ERROR_FAILURE;
  nsCOMPtr<nsILocalFile> f;
  nsAutoString strDBFile(dbGUID);

  PR_Lock(m_pDBStorePathLock);
  rv = NS_NewLocalFile(m_DBStorePath, PR_FALSE, getter_AddRefs(f));
  PR_Unlock(m_pDBStorePathLock);

  if(NS_FAILED(rv)) return rv;

  strDBFile.AppendLiteral(".db");
  rv = f->Append(strDBFile);
  if(NS_FAILED(rv)) return rv;
  rv = f->GetPath(strPath);
  if(NS_FAILED(rv)) return rv;

  return NS_OK;
} //GetDBStorePath
