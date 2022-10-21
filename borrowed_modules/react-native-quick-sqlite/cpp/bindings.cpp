#include "bindings.h"
#include "sqliteBridge.h"
#include "logs.h"
#include "JSIHelper.h"
#include "ThreadPool.h"
#include "sqlfileloader.h"
#include "sqlbatchexecutor.h"
#include <vector>
#include <string>
#include "macros.h"
#include "folly/dynamic.h"

#include <chrono>
#include <ctime>    

using namespace std;
using namespace facebook;

namespace osp {
string docPathStr;
std::shared_ptr<react::CallInvoker> invoker;

void install(jsi::Runtime &rt, std::shared_ptr<react::CallInvoker> jsCallInvoker, const char *docPath)
{
  docPathStr = std::string(docPath);
  auto pool = std::make_shared<ThreadPool>();
  invoker = jsCallInvoker;

  auto open = HOSTFN("open", 2) {
    if (count == 0)
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][open] database name is required");
    }

    if (!args[0].isString())
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][open] database name must be a string");
    }

    string dbName = args[0].asString(rt).utf8(rt);
    string tempDocPath = string(docPathStr);
    if (count > 1 && !args[1].isUndefined() && !args[1].isNull())
    {
      if (!args[1].isString())
      {
        throw jsi::JSError(rt, "[react-native-quick-sqlite][open] database location must be a string");
      }

      tempDocPath = tempDocPath + "/" + args[1].asString(rt).utf8(rt);
    }

    SQLiteOPResult result = sqliteOpenDb(dbName, tempDocPath);

    if (result.type == SQLiteError)
    {
      throw jsi::JSError(rt, result.errorMessage.c_str());
    }

    return {};
  });
  
  auto attach = HOSTFN("attach", 4) {
    if(count < 3) {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][attach] Incorrect number of arguments");
    }
    if (!args[0].isString() || !args[1].isString() || !args[2].isString())
    {
      throw jsi::JSError(rt, "dbName, databaseToAttach and alias must be a strings");
      return {};
    }

    string tempDocPath = string(docPathStr);
    if (count > 3 && !args[3].isUndefined() && !args[3].isNull())
    {
      if (!args[3].isString())
      {
        throw jsi::JSError(rt, "[react-native-quick-sqlite][attach] database location must be a string");
      }

      tempDocPath = tempDocPath + "/" + args[3].asString(rt).utf8(rt);
    }

    string dbName = args[0].asString(rt).utf8(rt);
    string databaseToAttach = args[1].asString(rt).utf8(rt);
    string alias = args[2].asString(rt).utf8(rt);
    SQLiteOPResult result = sqliteAttachDb(dbName, tempDocPath, databaseToAttach, alias);

    if (result.type == SQLiteError)
    {
      throw jsi::JSError(rt, result.errorMessage.c_str());
    }

    return {};
  });
  
  auto detach = HOSTFN("detach", 2) {
    if(count < 2) {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][detach] Incorrect number of arguments");
    }
    if (!args[0].isString() || !args[1].isString())
    {
      throw jsi::JSError(rt, "dbName, databaseToAttach and alias must be a strings");
      return {};
    }

    string dbName = args[0].asString(rt).utf8(rt);
    string alias = args[1].asString(rt).utf8(rt);
    SQLiteOPResult result = sqliteDetachDb(dbName, alias);

    if (result.type == SQLiteError)
    {
      throw jsi::JSError(rt, result.errorMessage.c_str());
    }

    return {};
  });

  auto close = HOSTFN("close", 1)
  {
    if (count == 0)
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][close] database name is required");
    }

    if (!args[0].isString())
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][close] database name must be a string");
    }

    string dbName = args[0].asString(rt).utf8(rt);

    SQLiteOPResult result = sqliteCloseDb(dbName);

    if (result.type == SQLiteError)
    {
      throw jsi::JSError(rt, result.errorMessage.c_str());
    }

    return {};
  });

  auto remove = HOSTFN("delete", 2)
  {
    if (count == 0)
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][open] database name is required");
    }

    if (!args[0].isString())
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][open] database name must be a string");
    }

    string dbName = args[0].asString(rt).utf8(rt);

    string tempDocPath = string(docPathStr);
    if (count > 1 && !args[1].isUndefined() && !args[1].isNull())
    {
      if (!args[1].isString())
      {
        throw jsi::JSError(rt, "[react-native-quick-sqlite][open] database location must be a string");
      }

      tempDocPath = tempDocPath + "/" + args[1].asString(rt).utf8(rt);
    }


    SQLiteOPResult result = sqliteRemoveDb(dbName, tempDocPath);

    if (result.type == SQLiteError)
    {
      throw jsi::JSError(rt, result.errorMessage.c_str());
    }

    return {};
  });

  auto execute = HOSTFN("execute", 3)
  {
    const string dbName = args[0].asString(rt).utf8(rt);
    const string query = args[1].asString(rt).utf8(rt);
    vector<QuickValue> params;
    if(count == 3) {
      const jsi::Value &originalParams = args[2];
      jsiQueryArgumentsToSequelParam(rt, originalParams, &params);
    }

    vector<map<string, QuickValue>> results;
    vector<QuickColumnMetadata> metadata;
    auto status = sqliteExecute(dbName, query, &params, &results, &metadata);

    // Converting results into a JSI Response
    try {
      auto jsiResult = createSequelQueryExecutionResult(rt, status, &results, &metadata);
      return jsiResult;
    } catch(std::exception &e) {
      throw jsi::JSError(rt, e.what());
    }
  });

  auto executeAsync = HOSTFN("executeAsync", 3)
  {
    auto start = std::chrono::system_clock::now();
    if (count < 3)
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][executeAsync] Incorrect arguments for executeAsync");
    }

    const string dbName = args[0].asString(rt).utf8(rt);
    const string query = args[1].asString(rt).utf8(rt);
    const jsi::Value &originalParams = args[2];

    // Converting query parameters inside the javascript caller thread
    std::shared_ptr<vector<QuickValue>> params = make_shared<vector<QuickValue>>();
    jsiQueryArgumentsToSequelParam(rt, originalParams, &(*params));

      auto end = std::chrono::system_clock::now();

      auto d = std::chrono::duration_cast<std::chrono::microseconds>(end-start);
      LOGV(("0 JS perfx on native took " + to_string(d.count()/ 1000) + string("")).c_str());

    auto promiseCtr = rt.global().getPropertyAsFunction(rt, "Promise");
    auto promise = promiseCtr.callAsConstructor(rt, HOSTFN("executor", 2) {
      auto resolve = std::make_shared<jsi::Value>(rt, args[0]);
      auto reject = std::make_shared<jsi::Value>(rt, args[1]);

      auto task =
      [&rt, dbName, query, params, resolve, reject, start]()
      {
        try
        {
            auto start2 = std::chrono::system_clock::now();
          vector<map<string, QuickValue>> results;
          auto metadata = make_shared<vector<QuickColumnMetadata>>();
            auto start3 = std::chrono::system_clock::now();
          auto status = sqliteExecute(dbName, query, params.get(), &results, &(*metadata));
          auto end = std::chrono::system_clock::now();
          auto d = std::chrono::duration_cast<std::chrono::microseconds>(end-start);
          LOGV(("1 work perfx on native took " + to_string(d.count()/ 1000) + string("")).c_str());

            auto d2 = std::chrono::duration_cast<std::chrono::microseconds>(end-start2);
            LOGV(("2 work only perfx on native took " + to_string(d2.count()/ 1000) + string("")).c_str());

            auto d3 = std::chrono::duration_cast<std::chrono::microseconds>(end-start2);
            LOGV(("3 work only perfx on native took " + to_string(d3.count()/ 1000) + string("")).c_str());
          invoker->invokeAsync([&rt, start, results = make_shared<vector<map<string, QuickValue>>>(results), metadata, status_copy = move(status), resolve]
                               {
            auto jsiResult = createSequelQueryExecutionResult(rt, status_copy, results.get(), metadata.get());
            auto end = std::chrono::system_clock::now();

            auto d = std::chrono::duration_cast<std::chrono::microseconds>(end-start);
          LOGV(("perfx on native took " + to_string(d.count()/ 1000) + string("")).c_str());
    
            resolve->asObject(rt).asFunction(rt).call(rt, move(jsiResult)); });
        }
        catch (std::exception &exc)
        {
          invoker->invokeAsync([&rt, &exc] {
            jsi::JSError(rt, exc.what());
          });
        }
      };

      pool->queueWork(task);

      return {};
    }));

    return promise;
  });
  
  // Execute a batch of SQL queries in a transaction
  // Parameters can be: [[sql: string, arguments: any[] | arguments: any[][] ]]
  auto executeBatch = HOSTFN("executeBatch", 2)
  {
    if (sizeof(args) < 2)
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][executeBatch] - Incorrect parameter count");
    }

    const jsi::Value &params = args[1];
    if (params.isNull() || params.isUndefined())
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][executeBatch] - An array of SQL commands or parameters is needed");
    }
    const string dbName = args[0].asString(rt).utf8(rt);
    const jsi::Array &batchParams = params.asObject(rt).asArray(rt);
    vector<QuickQueryArguments> commands;
    jsiBatchParametersToQuickArguments(rt, batchParams, &commands);

    auto batchResult = sqliteExecuteBatch(dbName, &commands);
    if (batchResult.type == SQLiteOk)
    {
      auto res = jsi::Object(rt);
      res.setProperty(rt, "rowsAffected", jsi::Value(batchResult.affectedRows));
      return move(res);
    }
    else
    {
      throw jsi::JSError(rt, batchResult.message);
    }
  });
  
  auto executeBatchAsync = HOSTFN("executeBatchAsync", 2)
  {
    if (sizeof(args) < 2)
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][executeAsyncBatch] Incorrect parameter count");
      return {};
    }

    const jsi::Value &params = args[1];

    if (params.isNull() || params.isUndefined())
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][executeAsyncBatch] - An array of SQL commands or parameters is needed");
      return {};
    }

    const string dbName = args[0].asString(rt).utf8(rt);
    const jsi::Array &batchParams = params.asObject(rt).asArray(rt);

    vector<QuickQueryArguments> commands;
    jsiBatchParametersToQuickArguments(rt, batchParams, &commands);

    auto promiseCtr = rt.global().getPropertyAsFunction(rt, "Promise");
    auto promise = promiseCtr.callAsConstructor(rt, HOSTFN("executor", 2) {
      auto resolve = std::make_shared<jsi::Value>(rt, args[0]);
      auto reject = std::make_shared<jsi::Value>(rt, args[1]);

      auto task =
      [&rt, dbName, commands = make_shared<vector<QuickQueryArguments>>(commands), resolve, reject]()
      {
        try
        {
          // Inside the new worker thread, we can now call sqlite operations
          auto batchResult = sqliteExecuteBatch(dbName, commands.get());
          invoker->invokeAsync([&rt, batchResult = move(batchResult), resolve, reject]
                               {
            if(batchResult.type == SQLiteOk)
            {
              auto res = jsi::Object(rt);
              res.setProperty(rt, "rowsAffected", jsi::Value(batchResult.affectedRows));
              resolve->asObject(rt).asFunction(rt).call(rt, move(res));
            } else
            {
              throw jsi::JSError(rt, batchResult.message);
            } });
        }
        catch (std::exception &exc)
        {
          invoker->invokeAsync([&rt, reject, &exc]
                               {
            throw jsi::JSError(rt, exc.what());
          });
        }
      };
      pool->queueWork(task);

      return {};
    }));

    return promise;
  });

  auto loadFile = HOSTFN("loadFile", 2)
  {
    const string dbName = args[0].asString(rt).utf8(rt);
    const string sqlFileName = args[1].asString(rt).utf8(rt);

    const auto importResult = importSQLFile(dbName, sqlFileName);
    if (importResult.type == SQLiteOk)
    {
      auto res = jsi::Object(rt);
      res.setProperty(rt, "rowsAffected", jsi::Value(importResult.affectedRows));
      res.setProperty(rt, "commands", jsi::Value(importResult.commands));
      return move(res);
    }
    else
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][loadFile] Could not open file");
    }
  });
  
  // Load SQL File from disk in another thread
  auto loadFileAsync = HOSTFN("loadFileAsync", 2)
  {
    if (sizeof(args) < 2)
    {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][loadFileAsync] Incorrect parameter count");
      return {};
    }

    const string dbName = args[0].asString(rt).utf8(rt);
    const string sqlFileName = args[1].asString(rt).utf8(rt);

    auto promiseCtr = rt.global().getPropertyAsFunction(rt, "Promise");
    auto promise = promiseCtr.callAsConstructor(rt, HOSTFN("executor", 2) {
      auto resolve = std::make_shared<jsi::Value>(rt, args[0]);
      auto reject = std::make_shared<jsi::Value>(rt, args[1]);

      auto task =
      [&rt, dbName, sqlFileName, resolve, reject]()
      {
        try
        {
          const auto importResult = importSQLFile(dbName, sqlFileName);

          invoker->invokeAsync([&rt, result = move(importResult), resolve, reject]
                               {
            if(result.type == SQLiteOk)
            {
              auto res = jsi::Object(rt);
              res.setProperty(rt, "rowsAffected", jsi::Value(result.affectedRows));
              res.setProperty(rt, "commands", jsi::Value(result.commands));
              resolve->asObject(rt).asFunction(rt).call(rt, move(res));
            } else {
              throw jsi::JSError(rt, result.message);
            } });
        }
        catch (std::exception &exc)
        {
          //          LOGW("Catched exception: %s", exc.what());
          invoker->invokeAsync([&rt, err = exc.what(), reject]
                               {
            throw jsi::JSError(rt, err);
          });
        }
      };
      pool->queueWork(task);
      return {};
    }));

    return promise;
  });



  jsi::Object module = jsi::Object(rt);

  module.setProperty(rt, "open", move(open));
  module.setProperty(rt, "close", move(close));
  module.setProperty(rt, "attach", move(attach));
  module.setProperty(rt, "detach", move(detach));
  module.setProperty(rt, "delete", move(remove));
  module.setProperty(rt, "execute", move(execute));
  module.setProperty(rt, "executeAsync", move(executeAsync));
  module.setProperty(rt, "executeBatch", move(executeBatch));
  module.setProperty(rt, "executeBatchAsync", move(executeBatchAsync));
  module.setProperty(rt, "loadFile", move(loadFile));
  module.setProperty(rt, "loadFileAsync", move(loadFileAsync));
  
  rt.global().setProperty(rt, "__QuickSQLiteProxy", move(module));
}

}
