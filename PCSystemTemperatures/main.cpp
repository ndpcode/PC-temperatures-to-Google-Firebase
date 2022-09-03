
//*********************************************************************************************************//
//Test project. Information about PC temperatures to google firebase database
//Created 30.12.2021
//Created by Novikov Dmitry
//only Win32 (Debug x86)
//*********************************************************************************************************//

#include "FirebaseEasyAdapter.h"
#include "PCTemperaturesScanner.h"
#include <fstream>
#include <sstream>
#include <functional>

int main(int argc, char* argv[])
{
	std::cout << "Enter \"exit\" to exit program." << std::endl;

	//google firebase driver
	FBEasy::FirebaseDBEasyAdapter testAdapter;

	//loading google config
	std::ifstream jsonConfigFile("google-services.json");
	//if (!jsonConfigFile.is_open())
	//{
	//	jsonConfigFile.open("google-services.json");
	//}
	if (!jsonConfigFile.is_open())
	{
		std::cout << "Can't load Firebase JSON config file" << std::endl;
		std::cout << "Exit now..." << std::endl;
		return 0;
	}
	std::stringstream configFileStream;
	configFileStream << jsonConfigFile.rdbuf();
	jsonConfigFile.close();

	//config client and connect
	if (!testAdapter.ConfigClient("PC-TEST-LENOVO",
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

	//instead of mutex...
	std::atomic_bool testFlag = false;

	//PCTemperaturesScanner::GPUZTemperatures gpuzTemper;
	PCTemperaturesScanner::AIDA64Temperatures gpuzTemper;
	std::map<std::string, double> temperValues{};

	//work while not enter "exit"
	std::function<void(bool)> setHandler = [&](bool res)
	{
		//std::cout << "+++ Success set new value for key" << std::endl;
		testFlag = true;
	};
	std::function<void(std::string&)> getHandler = [&](std::string& res)
	{
		//std::cout << "Current value of element = \"" << res << "\"" << std::endl;
		//if (res == "reboot_ok")
		//{
		//	system("shutdown -r -t 0");
		//}
		testFlag = true;
	};

	std::string inputStr = "";
	while (inputStr != "exit")
	{
		//get temperatures
		gpuzTemper.UpdateTemperatures();
		gpuzTemper.GetTemperatures(temperValues);
		//and send to database
		if (temperValues.size())
		{
			for (const auto& sensor : temperValues)
			{
				testFlag = false;
				testAdapter.SetElementValue(std::string("TemperatureSensors\\"),
					sensor.first,
					std::to_string(sensor.second),
					setHandler);
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}

		std::this_thread::sleep_for(std::chrono::seconds(2));

		//inputStr = exit...
	}

	testAdapter.DisconnectFromFirebase();

	system("pause");
	return 0;
}