
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <functional>

#include "FirebaseEasyAdapter.h"

int main()
{
	std::cout << "Enter \"exit\" to exit program." << std::endl;

	FBEasy::FirebaseDBEasyAdapter testAdapter;

	std::ifstream jsonConfigFile("C:\\SoftwareRepository\\BSFirebaseClient\\Debug\\google-services.json");
	if (!jsonConfigFile.is_open())
	{
		std::cout << "Can't load Firebase JSON config file" << std::endl;
		std::cout << "Exit now..." << std::endl;
		return 0;
	}
	std::stringstream configFileStream;
	configFileStream << jsonConfigFile.rdbuf();
	jsonConfigFile.close();

	if (!testAdapter.ConfigClient("Second client",
		"sec_cl@mail.com",
		"0q2jrc9tfh",
		configFileStream.str()))
	{
		std::cout << "testAdapter: ConfigClient fail." << std::endl;
		std::cout << "Exit now..." << std::endl;
		return 0;
	}
	if (!testAdapter.ConnectToFirebase())
	{
		std::cout << "testAdapter: ConnectToFirebase fail." << std::endl;
		std::cout << "Exit now..." << std::endl;
		return 0;
	}

	std::atomic_bool testFlag = false;

	//work while not enter "exit"
	std::function<void(bool)> setHandler = [&](bool res)
	{
		std::cout << "+++ Success set new value for key \"input_data\"" << std::endl;
		testFlag = false;
	};
	std::function<void(std::string&)> getHandler = [&](std::string& res)
	{
		std::cout << "Current value of element = \"" << res << "\"" << std::endl;
		testFlag = true;
	};
	std::string inputStr = "";
	while (inputStr != "exit")
	{
		if (inputStr.size())
		{
			testAdapter.GetElementValue(std::string("test\\getline\\"),
				std::string("input_data"),
				getHandler);
			while (!testFlag)
			{
				std::this_thread::sleep_for(std::chrono::seconds(10));
			}
			testAdapter.SetElementValue(std::string("test\\getline\\"),
				std::string("input_data"),
				inputStr,
				setHandler);
		}
		std::getline(std::cin, inputStr);
	}

	testAdapter.DisconnectFromFirebase();

	system("pause");
	return 0;
}
