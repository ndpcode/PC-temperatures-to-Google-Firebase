//*********************************************************************************************************//
//PCTemperaturesScanner header file
//Get information about system temperatures from different sources
//Created 24.12.2021
//Created by Novikov Dmitry
//*********************************************************************************************************//

#pragma once

#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <regex>
#include <windows.h>

/* separate namespace */
namespace PCTemperaturesScanner
{
	/* main class (base) */
	class PCTemperaturesData
	{
	protected:
		//pair: sensor name and value
		std::map<std::string, double> temperSensorsData = {};

	private:
		//dummy for debug messages output
		virtual void DebugMessage(std::string msgText)
		{
		}

	public:
		PCTemperaturesData()
		{
		}
		virtual ~PCTemperaturesData()
		{
		}

		//for override - update all sensors data
		virtual bool UpdateTemperatures()
		{
			return false;
		}

		//copy array with sensors data to target
		bool GetTemperatures(std::map<std::string, double>& data)
		{
			if (temperSensorsData.size())
			{
				data = temperSensorsData;
				return true;
			}
			return false;
		}

		//find and return sensor data by his name
		bool GetTemperValByKey(std::string& key, double& val)
		{
			std::map<std::string, double>::iterator findedEl = temperSensorsData.find(key);
			if (findedEl != temperSensorsData.end())
			{
				val = findedEl->second;
				return true;
			}
			return false;
		}
	};

	/* GPUZ reader */
	/* get temperatures data from GPUZ */
	class GPUZTemperatures : public PCTemperaturesData
	{
	private:
		//const and structures for work with GPUZ memory
		static constexpr int GPUZ_RECORDS_COUNT = 128;
#pragma pack(push, 1)
		struct GPUZ_RECORD
		{
			WCHAR key[256];
			WCHAR value[256];
		};

		struct GPUZ_SENSOR_RECORD
		{
			WCHAR name[256];
			WCHAR unit[8];
			UINT32 digits;
			double value;
		};

		struct GPUZ_SH_MEM
		{
			UINT32 version;		// Version number, 1 for the struct here
			volatile LONG busy;	// Is data being accessed?
			UINT32 lastUpdate;	// GetTickCount() of last update
			GPUZ_RECORD data[GPUZ_RECORDS_COUNT];
			GPUZ_SENSOR_RECORD sensors[GPUZ_RECORDS_COUNT];
		};
#pragma pack(pop)

		void DebugMessage(std::string msgText) override
		{
			std::cout << "GPUZTemperatures::UpdateTemperatures: " << msgText << std::endl;
		}

	public:
		GPUZTemperatures()
		{
		}
		~GPUZTemperatures()
		{
		}

		//get data from GPUZ throught shared memory
		bool UpdateTemperatures() override
		{
			HANDLE hMapFile;
			LPCTSTR pBuf;

			hMapFile = OpenFileMapping(
				FILE_MAP_ALL_ACCESS, // read/write access
				FALSE, // do not inherit the name
				L"GPUZShMem"); // name of mapping object

			if (hMapFile == NULL)
			{
				DebugMessage("Could not open file mapping object (" + std::to_string(static_cast<int>(GetLastError())) + ")");
				return false;
			}

			pBuf = (LPTSTR)MapViewOfFile(hMapFile, // handle to map object
				FILE_MAP_ALL_ACCESS, // read/write permission
				0,
				0,
				sizeof(GPUZ_SH_MEM));

			if (pBuf == NULL)
			{
				DebugMessage("Could not map view of file (" + std::to_string(static_cast<int>(GetLastError())) + ")");
				CloseHandle(hMapFile);
				return false;
			}

			std::unique_ptr<GPUZ_SH_MEM> gpuData = std::make_unique<GPUZ_SH_MEM>();
			if (!gpuData)
			{
				DebugMessage("No memory for read data stream.");

				UnmapViewOfFile(pBuf);
				CloseHandle(hMapFile);

				return false;
			}

			//copy shared memory to buffer
			CopyMemory(gpuData.get(), pBuf, sizeof(GPUZ_SH_MEM));
			//and close acces to shared memory
			UnmapViewOfFile(pBuf);
			CloseHandle(hMapFile);

			//parse buffer to array of sensors and data
			for (int i = 0; i < GPUZ_RECORDS_COUNT; i++)
			{
				if (wcsstr(gpuData->sensors[i].name, L"Temperature") != nullptr)
				{
					std::vector<char> charBuf(256);
					WideCharToMultiByte(CP_ACP, 0, gpuData->sensors[i].name, -1, &charBuf[0], 256, NULL, NULL);
					temperSensorsData[std::string(charBuf.begin(), charBuf.end())] = gpuData->sensors[i].value;
				}
			}

			return true;
		}
	};

	/* AIDA64 reader */
	/* get temperatures data from AIDA64 */
	class AIDA64Temperatures : public PCTemperaturesData
	{
	private:

		//static const std::wstring SharedMemName = L"AIDA64_SensorValues";

		void DebugMessage(std::string msgText) override
		{
			std::cout << "AIDA64Temperatures::UpdateTemperatures: " << msgText << std::endl;
		}

	public:
		AIDA64Temperatures()
		{
		}
		~AIDA64Temperatures()
		{
		}

		//get data from AIDA64 throught shared memory
		bool UpdateTemperatures() override
		{
			HANDLE hMapFile;
			LPCSTR pBuf;

			hMapFile = OpenFileMapping(
				FILE_MAP_READ, // read access
				FALSE, // do not inherit the name
				L"AIDA64_SensorValues"); // name of mapping object

			if (hMapFile == NULL)
			{
				DebugMessage("Could not open file mapping object (" + std::to_string(static_cast<int>(GetLastError())) + ")");
				return false;
			}

			pBuf = (LPSTR)MapViewOfFile(hMapFile, // handle to map object
				FILE_MAP_READ, // read permission
				0,
				0,
				0);

			if (pBuf == NULL)
			{
				DebugMessage("Could not map view of file (" + std::to_string(static_cast<int>(GetLastError())) + ")");

				CloseHandle(hMapFile);

				return false;
			}

			//create std::string with info from shared memory
			std::string sensorsData(pBuf);
			//and close access to shared memory
			UnmapViewOfFile(pBuf);
			CloseHandle(hMapFile);

			/* parsing variant with using regex */
			auto parseAIDA64Str = [this](std::string& sensStr)
			{
				auto getDataStrByRegex = [](const std::string& str, const std::regex& regExpr)->std::string
				{
					std::smatch regexMatch;
					if (std::regex_search(str, regexMatch, regExpr))
					{
						return regexMatch[1];
					}
					else
					{
						return "";
					}
				};
				std::regex regexTemp("<temp>(.*?)<\/temp>");
				std::smatch regTempMatch;
				std::string::const_iterator sensStrStart(sensStr.cbegin());
				while (std::regex_search(sensStrStart, sensStr.cend(), regTempMatch, regexTemp))
				{
					this->temperSensorsData[
						getDataStrByRegex(regTempMatch[1], std::regex("<label>(.*?)<\/label>"))
					] = std::stod(getDataStrByRegex(regTempMatch[1], std::regex("<value>(.*?)<\/value>")));
						sensStrStart = regTempMatch.suffix().first;
				}
			};

			//parsing
			parseAIDA64Str(sensorsData);

			return false;
		}
	};
}