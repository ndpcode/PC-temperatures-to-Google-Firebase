//*********************************************************************************************************//
//Firebase Easy Adapter header file
//Idea: user-friendly/simple wrapper for google firebase or other similar
//Created 20.05.2021
//Created by Novikov Dmitry
//*********************************************************************************************************//

#ifndef FIREBASE_EASY_ADAPTER
#define FIREBASE_EASY_ADAPTER

#include "firebase/app.h"
#include "firebase/auth.h"
#include "firebase/database.h"
#include "firebase/future.h"
#include "firebase/util.h"

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <functional>
#include <algorithm>
#include "windows.h"

namespace FBEasy
{
	using std::string;
	using std::unique_ptr;
	using std::shared_ptr;
	using std::thread;
	using std::mutex;
	using std::unique_lock;
	using std::lock_guard;
	using std::function;

	//functions result codes
	enum class FBEasyResult
	{
		FBE_RES_OK = 1,
		FBE_INPUT_PARAM_ERROR = -1,
		FBE_CLIENT_NAME_IS_EMPTY = -2,
		FBE_CLIENT_EMAIL_IS_EMPTY = -3,
		FBE_CLIENT_PASSWORD_IS_EMPTY = -4,		
		FBE_FIREBASE_JSON_CONFIG_IS_EMPTY = -5,
		FBE_CLIENT_ALREADY_WORK = -6,
		FBE_CANT_START_CLIENT_THREAD = -7,
		FBE_RETURNED_PARAM_IS_NULL = -8,
		FBE_KEY_VALUE_IS_EMPTY = -9,
		FBE_DB_TRANSACTION_ALREADY_ACTIVE = -10,
		FBE_MEMORY_ALLOC_OPERATION_ERROR = -11,
		FBE_UNSUPPORTED_VALUE_DATA_TYPE = -12,
		FBE_DBSET_PROCESS_INPUT_PARAMS_ERROR = -13,
		FBE_DBSET_PROCESS_DB_ACCESS_ERROR = -14,
		FBE_DBSET_PROCESS_DB_SETVAL_ERROR = -15,
		FBE_DBGET_PROCESS_INPUT_PARAMS_ERROR = -16,
		FBE_DBGET_PROCESS_DB_ACCESS_ERROR = -17,
		FBE_DBGET_PROCESS_DB_GETVAL_ERROR = -18,
		FBE_DBGET_PROCESS_REQ_TYPE_NOT_MATCH_DB_TYPE = -19,
		FBE_RES_DEFAULT = FBE_RES_OK
	};

	//class for easy firebase database access
	class FirebaseDBEasyAdapter
	{
		private:
			//simple struct to combine data and mutex
			template <typename dataType>
			struct syncData
			{
				dataType sValue;
				mutex sMutex;
			};

			//last result code for last called function
			mutable FBEasyResult lastErrorCode = FBEasyResult::FBE_RES_DEFAULT;
			//database client name, email, password
			string clientName = "", clientEMail = "", clientPassword = "";
			//firebase json config
			string firebaseJSONConfig = "";
			//database client thread object
			thread* clientThread = nullptr;
			//database client thread update period, msec
			int clientThreadUpdatePeriod = 10;
			//flag and mutex for database client thread state
			syncData<bool> clientThreadWork = {.sValue = false};
			//mutex - iostream
			mutex mutex_IOStream;
			//handlers called on completion of functions set and get
			using setOnComplHandler = function<void(bool)>;
			template <typename dataType>
			using getOnComplHandler = function<void(dataType&)>;
			//struct and mutex for exchange value with database
			struct dbExchangeData
			{
				enum class DBValueType
				{
					DB_VALUE_TYPE_NONE = 0,
					DB_VALUE_TYPE_INT,
					DB_VALUE_TYPE_STRING
				};
				enum class DBTransactionType
				{
					DB_TRANSACTION_NONE = 0,
					DB_TRANSACTION_SET,
					DB_TRANSACTION_GET
				};
				//transaction state flag and transaction type
				bool transactionActive = false;
				DBTransactionType transactionType = DBTransactionType::DB_TRANSACTION_NONE;
				//type of value and pointer to dynamically allocated object
				DBValueType valueType = DBValueType::DB_VALUE_TYPE_NONE;
				shared_ptr<void> value = nullptr;
				//path to database key and name of key
				string path = "";
				string key = "";
				//name of client - copy for thread
				string clientName = "";
				//pointer to handler, called after transaction
				void* onComplHandler = nullptr;
				//function for reset struct data
				void clear()
				{
					if (value != nullptr)
					{
						value.reset();
					}
					valueType = DBValueType::DB_VALUE_TYPE_NONE;
					transactionActive = false;
					transactionType = DBTransactionType::DB_TRANSACTION_NONE;
					path = "";
					key = "";
					clientName = "";
					onComplHandler = nullptr;
				}
			};			
			syncData <dbExchangeData> clientExchangeData;
			
			//function for check one parameter
			inline bool assert_param(const string& str, FBEasyResult errCode)
			{
				if (str.empty())
				{
					lastErrorCode = errCode;
					return false;
				}
				return true;
			}

		public:
			FirebaseDBEasyAdapter()
			{
			}
			~FirebaseDBEasyAdapter()
			{
				//close thread
				clientThreadClose();
				//clear transaction data
				clientExchangeData.sValue.clear();
			}

			//client config function - set parameters
			bool ConfigClient(const string& cName,
				const string& cEMail,
				const string& cPassword,
				const string& cfgData)
			{
				//check input params
				if (!assert_param(cName, FBEasyResult::FBE_CLIENT_NAME_IS_EMPTY) ||
					!assert_param(cEMail, FBEasyResult::FBE_CLIENT_EMAIL_IS_EMPTY) ||
					!assert_param(cPassword, FBEasyResult::FBE_CLIENT_PASSWORD_IS_EMPTY) ||
					!assert_param(cfgData, FBEasyResult::FBE_FIREBASE_JSON_CONFIG_IS_EMPTY))
				{
					return false;
				}
				//save
				clientName = cName;
				clientEMail = cEMail;
				clientPassword = cPassword;
				firebaseJSONConfig = cfgData;
				return true;
			}
			//get client name
			const string& GetClientName() const
			{
				//check client name
				if (clientName.empty())
				{
					lastErrorCode = FBEasyResult::FBE_CLIENT_NAME_IS_EMPTY;
				}
				return clientName;
			}
			//get client email
			const string& GetClientEMail() const
			{
				//check client email
				if (clientEMail.empty())
				{
					lastErrorCode = FBEasyResult::FBE_CLIENT_EMAIL_IS_EMPTY;
				}
				return clientEMail;
			}
			//get Firebase JSON config string
			const string& GetFirebaseJSONConfig() const
			{
				if (firebaseJSONConfig.empty())
				{
					lastErrorCode = FBEasyResult::FBE_FIREBASE_JSON_CONFIG_IS_EMPTY;
				}
				return firebaseJSONConfig;
			}

			//connect to firebase server
			bool ConnectToFirebase();

			//disconnect from firebase server
			bool DisconnectFromFirebase();

			//*********************************************************************************************************//
			/* set value for one database element */
			template <typename elemDataType>
			bool SetElementValue(const string& path,
				const string& key,
				const elemDataType& value,
				setOnComplHandler& onComplHandler = nullptr)
			{
				//check input params
				if (!assert_param(key, FBEasyResult::FBE_KEY_VALUE_IS_EMPTY))
				{
					return false;
				}

				//lock db exchange data struct
				lock_guard<mutex> dbExDataLock(clientExchangeData.sMutex);
				//check - already active or not
				if (clientExchangeData.sValue.transactionActive)
				{
					lastErrorCode = FBEasyResult::FBE_DB_TRANSACTION_ALREADY_ACTIVE;
					return false;
				}
				//set params
				try
				{
					//clear .value pointer
					if (clientExchangeData.sValue.value != nullptr)
					{
						clientExchangeData.sValue.value.reset();
					}
					//set datatype
					if (typeid(elemDataType) == typeid(int))
					{						
						clientExchangeData.sValue.valueType = dbExchangeData::DBValueType::DB_VALUE_TYPE_INT;
					}
					else if (typeid(elemDataType) == typeid(string))
					{
						clientExchangeData.sValue.valueType = dbExchangeData::DBValueType::DB_VALUE_TYPE_STRING;
					}
					else
					{
						lastErrorCode = FBEasyResult::FBE_UNSUPPORTED_VALUE_DATA_TYPE;
						return false;
					}
					//try alloc memory for new object
					clientExchangeData.sValue.value.reset(new elemDataType(value));
					//check again - after alloc memory
					if (clientExchangeData.sValue.value == nullptr)
					{
						throw - 1;
					}
				}
				catch (...)
				{
					lastErrorCode = FBEasyResult::FBE_MEMORY_ALLOC_OPERATION_ERROR;
					return false;
				}
				clientExchangeData.sValue.path = path;
				clientExchangeData.sValue.key = key;
				clientExchangeData.sValue.clientName = clientName;
				clientExchangeData.sValue.onComplHandler = reinterpret_cast<void*>(&onComplHandler);
				//set transaction active flag
				clientExchangeData.sValue.transactionType = dbExchangeData::DBTransactionType::DB_TRANSACTION_SET;
				clientExchangeData.sValue.transactionActive = true;

				//free mutex and exit
				return true;
			}
			//*********************************************************************************************************//

			//*********************************************************************************************************//
			/* get value of one database element */
			template <typename elemDataType>
			bool GetElementValue(const string& path,
				const string& key,
				getOnComplHandler<elemDataType>& onComplHandler = nullptr)
			{
				//check input params
				if (!assert_param(key, FBEasyResult::FBE_KEY_VALUE_IS_EMPTY))
				{
					return false;
				}
				if (onComplHandler == nullptr)
				{
					lastErrorCode = FBEasyResult::FBE_INPUT_PARAM_ERROR;
					return false;
				}
				
				//lock db exchange data struct
				lock_guard<mutex> dbExDataLock(clientExchangeData.sMutex);
				//check - already active or not
				if (clientExchangeData.sValue.transactionActive)
				{
					lastErrorCode = FBEasyResult::FBE_DB_TRANSACTION_ALREADY_ACTIVE;
					return false;
				}

				//set params
				//value data type
				if (typeid(elemDataType) == typeid(int))
				{
					clientExchangeData.sValue.valueType = dbExchangeData::DBValueType::DB_VALUE_TYPE_INT;
				}
				else if (typeid(elemDataType) == typeid(string))
				{
					clientExchangeData.sValue.valueType = dbExchangeData::DBValueType::DB_VALUE_TYPE_STRING;
				}
				else
				{
					lastErrorCode = FBEasyResult::FBE_UNSUPPORTED_VALUE_DATA_TYPE;
					return false;
				}
				//value pointer
				if (clientExchangeData.sValue.value != nullptr)
				{
					clientExchangeData.sValue.value.reset();
				}
				//path, key, client name
				clientExchangeData.sValue.path = path;
				clientExchangeData.sValue.key = key;
				clientExchangeData.sValue.clientName = clientName;
				//get database element value on complete handler
				clientExchangeData.sValue.onComplHandler = reinterpret_cast<void*>(&onComplHandler);
				//set transaction active flag
				clientExchangeData.sValue.transactionType = dbExchangeData::DBTransactionType::DB_TRANSACTION_GET;
				clientExchangeData.sValue.transactionActive = true;

				return true;
			}
			//*********************************************************************************************************//
		
		private:
			//function for write to log
			void writeToLog(const string& message)
			{
				lock_guard<mutex> lock(mutex_IOStream);
				std::cout << "Firebase client: " << message << std::endl;
			}

			//function - wait and event handling
			void waitAndEvents(int msec)
			{
				Sleep(msec);
			}
			
			//wait operation completion and return message if need
			bool waitForCompletion(const firebase::FutureBase& future, const string& operationName)
			{
				while (future.status() == firebase::kFutureStatusPending)
				{
					waitAndEvents(100);
					//check thread start/stop flag
					unique_lock<mutex> lock_clientThreadWork(clientThreadWork.sMutex);
					if (!clientThreadWork.sValue)
					{
						//stop
						return false;
					}
					lock_clientThreadWork.unlock();
				}
				if (future.status() != firebase::kFutureStatusComplete)
				{
					writeToLog("ERROR: " + operationName + " returned an invalid result.");
				}
				else if (future.error() != 0)
				{
					writeToLog("ERROR: " + operationName + " returned error " + std::to_string(future.error()) + ": " + string(future.error_message()));
				}
				return true;
			}

			//client thread function
			void clientThreadProcess();

			//function for close client thread
			void clientThreadClose();

			//util function - access to database element using path and key
			bool getDBRefFromPath(const string& path, const string& key, const string& clName,
				const firebase::database::Database& database, firebase::database::DatabaseReference& dbRef);

			//function for process "set" database value
			void clientThreadProcessSET(const firebase::database::Database& fbDatabase);

			//function for process "get" database value
			void clientThreadProcessGET(const firebase::database::Database& fbDatabase);
	};
}

#endif
