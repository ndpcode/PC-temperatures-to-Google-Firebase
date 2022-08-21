//*********************************************************************************************************//
//Firebase Easy Adapter source file
//Idea: user-friendly/simple wrapper for google firebase or other similar
//Created 20.05.2021
//Created by Novikov Dmitry
//*********************************************************************************************************//

#include "FirebaseEasyAdapter.h"

using namespace FBEasy;

//*********************************************************************************************************//
/* client thread function */
void FirebaseDBEasyAdapter::clientThreadProcess()
{
	//set client thread work flag
	unique_lock<mutex> lock_clientThreadWork(clientThreadWork.sMutex);
	clientThreadWork.sValue = true;
	lock_clientThreadWork.unlock();

	//init google firebase >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
	//app init
	writeToLog("Start initialize Firebase App...");
	::firebase::AppOptions clientAppOptions;
	unique_ptr<::firebase::App> app = nullptr;
	try
	{
		//try init firebase config
		writeToLog("Load Firebase JSON config...");
		if (::firebase::AppOptions::LoadFromJsonConfig(firebaseJSONConfig.c_str(), &clientAppOptions) == nullptr)
		{
			throw -1;
		}
		writeToLog("Load Firebase JSON config - OK");
		//try init firebase app
		app.reset(::firebase::App::Create(clientAppOptions));
		if (app.get() == nullptr || app.get()->GetInstance() == nullptr)
		{
			throw -1;
		}
	}
	catch (...)
	{
		//thread shutdown
		clientThreadClose();
		writeToLog("Failed to initialize Firebase App");
		writeToLog("Client thread closed");
		return;
	}
	writeToLog("Initialize Firebase App - OK");

	//auth and database init
	writeToLog("Initialize Firebase Auth and Firebase Database...");
	unique_ptr<::firebase::auth::Auth> auth = nullptr;
	unique_ptr<::firebase::database::Database> database = nullptr;    
	void* initialize_targets[] = {&auth, &database};
	const firebase::ModuleInitializer::InitializerFn initializers[] =
	{
		[](::firebase::App* app, void* data)
		{
			void** arr = reinterpret_cast<void**>(data);
			unique_ptr<::firebase::auth::Auth>* auth = reinterpret_cast<unique_ptr<::firebase::auth::Auth>*>(arr[0]);
			::firebase::InitResult result;
			auth->reset(::firebase::auth::Auth::GetAuth(app, &result));
			return result;
		},
		[](::firebase::App* app, void* data)
		{
			void** arr = reinterpret_cast<void**>(data);
			unique_ptr<::firebase::database::Database>* database = reinterpret_cast<unique_ptr<::firebase::database::Database>*>(arr[1]);
			::firebase::InitResult result;
			database->reset(::firebase::database::Database::GetInstance(app, &result));
			return result;
		}
	};
	::firebase::ModuleInitializer initializer;
	initializer.Initialize(app.get(), initialize_targets, initializers, sizeof(initializers) / sizeof(initializers[0]));
	if (!waitForCompletion(initializer.InitializeLastResult(), "Initialize Firebase Auth and Firebase Database process"))
	{
		//thread shutdown
		clientThreadClose();
		writeToLog("Client thread closed");
		return;
	}
	if (initializer.InitializeLastResult().error() != 0 ||
		auth == nullptr ||
		database == nullptr)
	{
		string errMsg(initializer.InitializeLastResult().error_message());
		writeToLog("Failed to initialize Firebase libraries: " + errMsg);
		waitAndEvents(2000);
		//thread shutdown
		clientThreadClose();
		writeToLog("Client thread closed");
		return;
	}
	writeToLog("Initialize Firebase Auth and Firebase Database - OK");

	database->set_persistence_enabled(true);

	//sign in
	writeToLog("Auth: sign in...");
	firebase::Future<firebase::auth::User*> signInFuture =
		auth->SignInWithEmailAndPassword(clientEMail.c_str(), clientPassword.c_str());
	if (!waitForCompletion(signInFuture, "Sign in process"))
	{
		//thread shutdown
		clientThreadClose();
		writeToLog("Client thread closed");
		return;
	}
	//if client (user) not exists
	if (signInFuture.error() == firebase::auth::kAuthErrorUserNotFound)
	{
		writeToLog("Auth: client with specified email not found");
		writeToLog("Auth: try register new client...");
		signInFuture = auth->CreateUserWithEmailAndPassword(clientEMail.c_str(), clientPassword.c_str());
		if (!waitForCompletion(signInFuture, "Register client and sign in process"))
		{
			//thread shutdown
			clientThreadClose();
			writeToLog("Client thread closed");
			return;
		}
	}
	if (signInFuture.error() == firebase::auth::kAuthErrorNone)
	{
		writeToLog("Auth: signed in as: " + clientName + " & " + clientEMail);
	}
	else
	{
		writeToLog("ERROR: Could not sign in. Error " + std::to_string(signInFuture.error()) + ": " + signInFuture.error_message());
		writeToLog("Ensure your application has the email sign-in provider enabled in Firebase Console.");
		//thread shutdown
		clientThreadClose();
		writeToLog("Client thread closed");
		return;
	}
	
	//create or open a unique child in the database, key name = this->clientName
	firebase::database::DatabaseReference dbRef;
	dbRef = database->GetReference(clientName.c_str());
	writeToLog("URL: " + dbRef.url());
	//init google firebase <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

	//write current time
	firebase::Future<void> currentTimeFunc = dbRef.Child("LastAuthTime").SetValue(firebase::database::ServerTimestamp());
	if (!waitForCompletion(currentTimeFunc, "Write current auth time to database process") ||
		currentTimeFunc.error() != firebase::database::kErrorNone)
	{
		//write error
		writeToLog("Write current time to database - ERROR");
		//thread shutdown
		clientThreadClose();
		writeToLog("Client thread closed");
		return;
	}

	unique_lock<mutex> dbExchangeLock(clientExchangeData.sMutex, std::defer_lock);
	while (1)
	{
		// ? std::condition_variable - ??
		//check database transactions state
		dbExchangeLock.lock();
		if (clientExchangeData.sValue.transactionActive)
		{
			if (clientExchangeData.sValue.transactionType == dbExchangeData::DBTransactionType::DB_TRANSACTION_SET)
			{
				//set
				if (database != nullptr)
				{
					clientThreadProcessSET(*database);
				}				
			}
			else if (clientExchangeData.sValue.transactionType == dbExchangeData::DBTransactionType::DB_TRANSACTION_GET)
			{
				//get
				if (database != nullptr)
				{
					clientThreadProcessGET(*database);
				}
			}
			else
			{
				//unknown
				writeToLog("Internal error: transaction active but unknown transaction type");
			}
		}
		dbExchangeLock.unlock();

		//check thread start/stop flag
		lock_clientThreadWork.lock();
		if (!clientThreadWork.sValue)
		{
			//stop
			lock_clientThreadWork.unlock();
			break;
		}
		lock_clientThreadWork.unlock();

		//pause
		std::this_thread::sleep_for(std::chrono::milliseconds(clientThreadUpdatePeriod));
	}

	//thread shutdown
	clientThreadClose();
	writeToLog("Client thread closed");
}
//*********************************************************************************************************//

//*********************************************************************************************************//
/* function for close client thread */
void FirebaseDBEasyAdapter::clientThreadClose()
{
	//set client thread work flag
	unique_lock<mutex> lock_clientThreadWork(clientThreadWork.sMutex);
	clientThreadWork.sValue = false;
	lock_clientThreadWork.unlock();

	//*****
	//here to add a check that thread really stopped
	//*****

	//zero thread pointer before exit
	clientThread = nullptr;
}
//*********************************************************************************************************//

//*********************************************************************************************************//
/* util function - access to database element using path and key */
bool FirebaseDBEasyAdapter::getDBRefFromPath(const string& path, const string& key, const string& clName,
	const firebase::database::Database& database, firebase::database::DatabaseReference& dbRef)
{
	//check input
	if (key.empty() || clName.empty())
	{
		return false;
	}
	//parent
	dbRef = database.GetReference(clName.c_str());
	//assign to child - path
	if (path.length())
	{
		//copy string and replace slash
		string pathCopy = path;
		std::replace(pathCopy.begin(), pathCopy.end(), '\\', '/');
		//find slash and get path element
		auto findNextEl = [](std::string& inputPath, std::string& onePathEl) -> bool
		{
			if (inputPath.empty())
			{
				return false;
			}
			auto slashPos = inputPath.find('/');
			if (slashPos == std::string::npos)
			{
				slashPos = inputPath.size();
				inputPath += '/';
			}
			onePathEl = inputPath.substr(0, slashPos);
			inputPath.erase(0, slashPos + 1);
			return true;
		};
		//find path elements and get database reference
		string pathEl = "";
		while (findNextEl(pathCopy, pathEl))
		{
			dbRef = dbRef.Child(pathEl);
		}
	}
	//... and key
	dbRef = dbRef.Child(key);

	return true;
};
//*********************************************************************************************************//

//*********************************************************************************************************//
/* function for process "set" database value */
void FirebaseDBEasyAdapter::clientThreadProcessSET(const firebase::database::Database& fbDatabase)
{
	//call this function only after lock clientExchangeData mutex!

	//set value for database element
	auto dbSetValueForRef = [&]<typename dataType>(firebase::database::DatabaseReference& dbRef, dataType& elVal) -> bool
	{
		//set value and get firebase future object
		firebase::Future<void> setTransactionProc = dbRef.SetValue(elVal);
		if (!waitForCompletion(setTransactionProc, "Exchange data process - set database value") ||
			setTransactionProc.error() != firebase::database::kErrorNone)
		{
			//write error
			writeToLog("Set database value - ERROR");
			return false;
		}
		return true;
	};

	//run "set" on complete handler
	auto onSetHandler = [&](bool param)
	{
		if (clientExchangeData.sValue.onComplHandler != nullptr)
		{
			setOnComplHandler* onSetH = reinterpret_cast<setOnComplHandler*>(clientExchangeData.sValue.onComplHandler);
			(*onSetH)(param);
		}
	};

	try
	{
		//check input data
		if (clientExchangeData.sValue.value == nullptr ||
			(clientExchangeData.sValue.valueType != dbExchangeData::DBValueType::DB_VALUE_TYPE_INT &&
				clientExchangeData.sValue.valueType != dbExchangeData::DBValueType::DB_VALUE_TYPE_STRING) ||
			clientExchangeData.sValue.key.empty() ||
			clientExchangeData.sValue.clientName.empty())
		{
			throw FBEasyResult::FBE_DBSET_PROCESS_INPUT_PARAMS_ERROR;
		}

		//access to database reference
		firebase::database::DatabaseReference dbSetRef;
		if (!getDBRefFromPath(clientExchangeData.sValue.path,
			clientExchangeData.sValue.key,
			clientExchangeData.sValue.clientName,
			fbDatabase,
			dbSetRef))
		{
			throw FBEasyResult::FBE_DBSET_PROCESS_DB_ACCESS_ERROR;
		}

		//set database element value
		int* intVal = nullptr;
		string* strVal = nullptr;
		switch (clientExchangeData.sValue.valueType)
		{
			case dbExchangeData::DBValueType::DB_VALUE_TYPE_INT:
				intVal = reinterpret_cast<int*>(clientExchangeData.sValue.value.get());
				if (intVal == nullptr || !dbSetValueForRef(dbSetRef, *intVal))
				{
					throw FBEasyResult::FBE_DBSET_PROCESS_DB_SETVAL_ERROR;
				}
			break;

			case dbExchangeData::DBValueType::DB_VALUE_TYPE_STRING:
				strVal = reinterpret_cast<string*>(clientExchangeData.sValue.value.get());
				if (strVal == nullptr || !dbSetValueForRef(dbSetRef, *strVal))
				{
					throw FBEasyResult::FBE_DBSET_PROCESS_DB_SETVAL_ERROR;
				}
			break;
		}

		//run on complete handler
		onSetHandler(true);
	}
	catch (FBEasyResult errCode)
	{
		//save error code

		//run on complete handler
		onSetHandler(false);
		//message
		writeToLog("Database SET value process - return error with code = " + std::to_string(static_cast<int>(errCode)));
	}
	catch (...)
	{
		//save error code - unknown error

		//run on complete handler
		onSetHandler(false);
		//message
		writeToLog("Database SET value process - return unknown error");
	}
	//clear
	clientExchangeData.sValue.clear();
}
//*********************************************************************************************************//

//*********************************************************************************************************//
/* function for process "get" database value */
void FirebaseDBEasyAdapter::clientThreadProcessGET(const firebase::database::Database& fbDatabase)
{
	//call this function only after lock clientExchangeData mutex!

	//run "get" on complete handler
	auto onGetHandler = [&]<typename dataType>(dataType& resValue)
	{
		//clientExchangeData.sValue.onComplHandler not nullptr!
		getOnComplHandler<dataType>* onGetH = static_cast<getOnComplHandler<dataType>*>(clientExchangeData.sValue.onComplHandler);
		(*onGetH)(resValue);
	};

	try
	{
		//check input data
		if (clientExchangeData.sValue.value != nullptr ||
			(clientExchangeData.sValue.valueType != dbExchangeData::DBValueType::DB_VALUE_TYPE_INT &&
				clientExchangeData.sValue.valueType != dbExchangeData::DBValueType::DB_VALUE_TYPE_STRING) ||
			clientExchangeData.sValue.key.empty() ||
			clientExchangeData.sValue.clientName.empty() ||
			clientExchangeData.sValue.onComplHandler == nullptr)
		{
			throw FBEasyResult::FBE_DBGET_PROCESS_INPUT_PARAMS_ERROR;
		}

		//access to database reference
		firebase::database::DatabaseReference dbGetRef;
		if (!getDBRefFromPath(clientExchangeData.sValue.path,
			clientExchangeData.sValue.key,
			clientExchangeData.sValue.clientName,
			fbDatabase,
			dbGetRef))
		{
			throw FBEasyResult::FBE_DBGET_PROCESS_DB_ACCESS_ERROR;
		}

		//get database value
		firebase::Future<firebase::database::DataSnapshot> getTransactionProc = dbGetRef.GetValue();
		if (!waitForCompletion(getTransactionProc, "Exchange data process - get database value") ||
			getTransactionProc.error() != firebase::database::kErrorNone)
		{
			//write error
			writeToLog("Get database value - ERROR");
			throw FBEasyResult::FBE_DBGET_PROCESS_DB_GETVAL_ERROR;
		}

		//parse returned value depending data type
		switch (clientExchangeData.sValue.valueType)
		{
			case dbExchangeData::DBValueType::DB_VALUE_TYPE_INT:
				//check requested type matching with database type
				if (!getTransactionProc.result()->value().is_int64())
				{
					throw FBEasyResult::FBE_DBGET_PROCESS_REQ_TYPE_NOT_MATCH_DB_TYPE;
				}
				//get value and run on complete handler
				{
					int intVal = static_cast<int>(getTransactionProc.result()->value().int64_value());
					onGetHandler(intVal);
				}
			break;

			case dbExchangeData::DBValueType::DB_VALUE_TYPE_STRING:
				//check requested type matching with database type
				if (!getTransactionProc.result()->value().is_string())
				{
					throw FBEasyResult::FBE_DBGET_PROCESS_REQ_TYPE_NOT_MATCH_DB_TYPE;
				}
				//get value and run on complete handler
				{
					string strVal(getTransactionProc.result()->value().string_value());
					onGetHandler(strVal);
				}
			break;
		}
	}
	catch (FBEasyResult errCode)
	{
		//save error code
		
		//message
		writeToLog("Database GET value process - return error with code = " + std::to_string(static_cast<int>(errCode)));
	}
	catch (...)
	{
		//save error code - unknown error
		
		//message
		writeToLog("Database GET value process - return unknown error");
	}
	//clear
	clientExchangeData.sValue.clear();
}
//*********************************************************************************************************//

//*********************************************************************************************************//
/* start database client thread and try connect */
bool FirebaseDBEasyAdapter::ConnectToFirebase()
{
	//check client config
	if (!assert_param(clientName, FBEasyResult::FBE_CLIENT_NAME_IS_EMPTY) ||
		!assert_param(clientEMail, FBEasyResult::FBE_CLIENT_EMAIL_IS_EMPTY) ||
		!assert_param(clientPassword, FBEasyResult::FBE_CLIENT_PASSWORD_IS_EMPTY) ||
		!assert_param(firebaseJSONConfig, FBEasyResult::FBE_FIREBASE_JSON_CONFIG_IS_EMPTY))
	{
		return false;
	}
	//check - thread already working
	unique_lock<mutex> lock_clientThreadWork(clientThreadWork.sMutex);
	if (clientThreadWork.sValue)
	{
		lastErrorCode = FBEasyResult::FBE_CLIENT_ALREADY_WORK;
		return false;
	}
	//release mutex
	lock_clientThreadWork.unlock();

	//start new thread
	try
	{
		clientThread = new thread([&]() {this->clientThreadProcess(); });
		if (clientThread == nullptr)
		{
			throw -1;
		}
	}
	catch (...)
	{
		lastErrorCode = FBEasyResult::FBE_CANT_START_CLIENT_THREAD;
		clientThread = nullptr;
		return false;
	}

	//detach thread
	//thurther work into thread
	clientThread->detach();

	return true;
}
//*********************************************************************************************************//

//*********************************************************************************************************//
/* disconnect client from firebase server */
bool FirebaseDBEasyAdapter::DisconnectFromFirebase()
{
	//message for stop thread
	clientThreadClose();
	return true;
}
//*********************************************************************************************************//