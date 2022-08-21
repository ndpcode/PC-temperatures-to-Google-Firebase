//*********************************************************************************************************//
//TEST: Get information about system temperatures from different sources
//Created 26.12.2021
//Created by Novikov Dmitry
//*********************************************************************************************************//

#include "PCTemperaturesScanner.h"
#include <map>

int main(int argc, char* argv[])
{
	//get from GPUZ
	PCTemperaturesScanner::GPUZTemperatures gpuzTemper;
	gpuzTemper.UpdateTemperatures();
	std::map<std::string, double> temperValuesGPUZ{};
	gpuzTemper.GetTemperatures(temperValuesGPUZ);

	//get from AIDA64
	PCTemperaturesScanner::AIDA64Temperatures aida64Temper;
	aida64Temper.UpdateTemperatures();
	std::map<std::string, double> temperValuesAIDA64{};
	aida64Temper.GetTemperatures(temperValuesAIDA64);

	system("pause");

	return 0;
}