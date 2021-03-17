/////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2020 William C Bonner
//
//	MIT License
//
//	Permission is hereby granted, free of charge, to any person obtaining a copy
//	of this software and associated documentation files(the "Software"), to deal
//	in the Software without restriction, including without limitation the rights
//	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//	copies of the Software, and to permit persons to whom the Software is
//	furnished to do so, subject to the following conditions :
//
//	The above copyright notice and this permission notice shall be included in all
//	copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//	SOFTWARE.
//
/////////////////////////////////////////////////////////////////////////////
// This code borrows from plenty of other sources I've found.
// I try to leave credits in comments scattered through the code itself and
// would appreciate similar credit if you use portions of my code.
/////////////////////////////////////////////////////////////////////////////
#include <cstring>
#include <cstdio>
#include <ctime>
#include <csignal>
#include <string>
#include <iostream>
#include <locale>
#include <queue>
#include <map>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>		// For socket(), connect(), send(), and recv()
#include <fcntl.h>
#include <netinet/in.h>		// For sockaddr_in
#include <arpa/inet.h>		// For inet_addr()
#include <ifaddrs.h>		// for getifaddrs()
#include <unistd.h>			// For close()
#include <netdb.h>			// For gethostbyname()
#include <sys/ioctl.h>
#include <getopt.h>
/////////////////////////////////////////////////////////////////////////////
// URLs with information:
// https://www.softscheck.com/en/reverse-engineering-tp-link-hs110/
// https://github.com/softScheck/tplink-smartplug
// https://blog.georgovassilis.com/2016/05/07/controlling-the-tp-link-hs100-wi-fi-smart-plug/
// https://github.com/ggeorgovassilis/linuxscripts/tree/master/tp-link-hs100-smartplug
// https://github.com/plasticrake/tplink-smarthome-api
// https://github.com/jamesbarnett91/tplink-energy-monitor
// https://github.com/python-kasa/python-kasa
/////////////////////////////////////////////////////////////////////////////
static const std::string ProgramVersionString("KasaEnergyLogger Version 1.20210317-1 Built on: " __DATE__ " at " __TIME__);
/////////////////////////////////////////////////////////////////////////////
std::string timeToISO8601(const time_t & TheTime)
{
	std::ostringstream ISOTime;
	struct tm UTC;
	if (0 != gmtime_r(&TheTime, &UTC))
	{
		ISOTime.fill('0');
		if (!((UTC.tm_year == 70) && (UTC.tm_mon == 0) && (UTC.tm_mday == 1)))
		{
			ISOTime << UTC.tm_year + 1900 << "-";
			ISOTime.width(2);
			ISOTime << UTC.tm_mon + 1 << "-";
			ISOTime.width(2);
			ISOTime << UTC.tm_mday << "T";
		}
		ISOTime.width(2);
		ISOTime << UTC.tm_hour << ":";
		ISOTime.width(2);
		ISOTime << UTC.tm_min << ":";
		ISOTime.width(2);
		ISOTime << UTC.tm_sec;
	}
	return(ISOTime.str());
}
std::string getTimeISO8601(void)
{
	time_t timer;
	time(&timer);
	std::string isostring(timeToISO8601(timer));
	std::string rval;
	rval.assign(isostring.begin(), isostring.end());

	return(rval);
}
time_t ISO8601totime(const std::string& ISOTime)
{
	struct tm UTC;
	UTC.tm_year = stol(ISOTime.substr(0, 4)) - 1900;
	UTC.tm_mon = stol(ISOTime.substr(5, 2)) - 1;
	UTC.tm_mday = stol(ISOTime.substr(8, 2));
	UTC.tm_hour = stol(ISOTime.substr(11, 2));
	UTC.tm_min = stol(ISOTime.substr(14, 2));
	UTC.tm_sec = stol(ISOTime.substr(17, 2));
	UTC.tm_gmtoff = 0;
	UTC.tm_isdst = -1;
	UTC.tm_zone = 0;
#ifdef _MSC_VER
	_tzset();
	_get_daylight(&(UTC.tm_isdst));
#endif
# ifdef __USE_MISC
	time_t timer = timegm(&UTC);
#else
	time_t timer = mktime(&UTC);
	timer -= timezone; // HACK: Works in my initial testing on the raspberry pi, but it's currently not DST
#endif
#ifdef _MSC_VER
	long Timezone_seconds = 0;
	_get_timezone(&Timezone_seconds);
	timer -= Timezone_seconds;
	int DST_hours = 0;
	_get_daylight(&DST_hours);
	long DST_seconds = 0;
	_get_dstbias(&DST_seconds);
	timer += DST_hours * DST_seconds;
#else
#endif
	return(timer);
}
// Microsoft Excel doesn't recognize ISO8601 format dates with the "T" seperating the date and time
// This function puts a space where the T goes for ISO8601. The dates can be decoded with ISO8601totime()
std::string timeToExcelDate(const time_t & TheTime)
{
	std::ostringstream ExcelDate;
	struct tm UTC;
	if (0 != gmtime_r(&TheTime, &UTC))
	{
		ExcelDate.fill('0');
		ExcelDate << UTC.tm_year + 1900 << "-";
		ExcelDate.width(2);
		ExcelDate << UTC.tm_mon + 1 << "-";
		ExcelDate.width(2);
		ExcelDate << UTC.tm_mday << " ";
		ExcelDate.width(2);
		ExcelDate << UTC.tm_hour << ":";
		ExcelDate.width(2);
		ExcelDate << UTC.tm_min << ":";
		ExcelDate.width(2);
		ExcelDate << UTC.tm_sec;
	}
	return(ExcelDate.str());
}
/////////////////////////////////////////////////////////////////////////////
void KasaEncrypt(const std::string &input, uint8_t * output)
{
	uint8_t key = 0xAB;
	for (auto it = input.begin(); it != input.end(); it++)
	{
		uint8_t a = key ^ *it;
		key = a;
		*output++ = a;
	}
}
void KasaDecrypt(const size_t len, const uint8_t input[], std::string &output)
{
	output.clear();
	output.reserve(len);	// minor optimization, not required but efficient
	uint8_t key = 0xAB;
	for (size_t index = 0; index < len; index++)
	{
		uint8_t a = key ^ input[index];
		key = input[index];
		output += a;
	}
}
/////////////////////////////////////////////////////////////////////////////
// https://github.com/softScheck/tplink-smartplug/blob/master/tplink-smarthome-commands.txt
const uint8_t KasaBroadcast[] =	"{\"system\":{\"get_sysinfo\":{}},\"emeter\":{\"get_realtime\":{}},\"smartlife.iot.common.emeter\":{\"get_realtime\":{}}}";
const uint8_t KasaWiFi[] =		"{\"netif\":{\"set_stainfo\":{\"ssid\":\"WiFi\", \"password\" : \"secret\", \"key_type\" : 3}}}";	// Connect to AP with given SSID and Password
/////////////////////////////////////////////////////////////////////////////
class CKasaClient {
public:
	struct sockaddr address;
	time_t date;
	std::string information;
	std::string GetDeviceID(void) const;
};
std::string CKasaClient::GetDeviceID(void) const
{
	std::string DeviceID;
	auto pos = information.find("\"deviceId\"");
	if (pos == std::string::npos)
		pos = information.find("\"id\"");
	if (pos != std::string::npos)
	{
		// HACK: I need to clean this up..  
		DeviceID = information.substr(pos);	// value starts at key
		DeviceID.erase(DeviceID.find_first_of(",}"));	// truncate value
		DeviceID.erase(0, DeviceID.find(':'));	// move past key value
		DeviceID.erase(DeviceID.find(':'), 1);	// move past seperator
		DeviceID.erase(DeviceID.find('"'), 1);	// erase leading quote
		DeviceID.erase(DeviceID.find('"'), 1);	// erase trailing quote
	}
	return(DeviceID);
}
/////////////////////////////////////////////////////////////////////////////
// The following operator was required so I could use the std::map<> to use CKasaClient as the key
bool operator <(const CKasaClient &a, const CKasaClient &b)
{
	std::string AdeviceId(a.GetDeviceID());
	std::string BdeviceId(b.GetDeviceID());
	return(AdeviceId.compare(BdeviceId) < 0);
}
/////////////////////////////////////////////////////////////////////////////
std::string LogDirectory("./");
std::string GenerateLogFileName(const std::string &DeviceID)
{
	std::ostringstream OutputFilename;
	OutputFilename << LogDirectory;
	OutputFilename << "kasa-";
	OutputFilename << DeviceID;
	time_t timer;
	time(&timer);
	struct tm UTC;
	if (0 != gmtime_r(&timer, &UTC))
		if (!((UTC.tm_year == 70) && (UTC.tm_mon == 0) && (UTC.tm_mday == 1)))
			OutputFilename << "-" << std::dec << UTC.tm_year + 1900 << "-" << std::setw(2) << std::setfill('0') << UTC.tm_mon + 1;
	OutputFilename << ".txt";
	return(OutputFilename.str());
}
bool GenerateLogFile(std::map<CKasaClient, std::queue<std::string>> &KasaMap)
{
	bool rval = false;
	for (auto it = KasaMap.begin(); it != KasaMap.end(); ++it)
	{
		if (!it->second.empty()) // Only open the log file if there are entries to add
		{
			std::ofstream LogFile(GenerateLogFileName(it->first.GetDeviceID()), std::ios_base::out | std::ios_base::app | std::ios_base::ate);
			if (LogFile.is_open())
			{
				while (!it->second.empty())
				{
					LogFile << it->second.front() << std::endl;
					it->second.pop();
				}
				LogFile.close();
				rval = true;
			}
		}
	}
	return(rval);
}
/////////////////////////////////////////////////////////////////////////////
void GetMRTGOutput(const std::string &DeviceID, const int Minutes)
{
	// HACK: This next bit of getting the time formatted the same way I log it and 
	// then converting it back to a time_t is a workaround for behavior I 
	// currently do not understand and don't want to spend further time on now.
	std::string ISOCurrentTime(getTimeISO8601());
	time_t now = ISO8601totime(ISOCurrentTime);
	std::ifstream TheFile(GenerateLogFileName(DeviceID));
	if (TheFile.is_open())
	{
		std::queue<std::string> LogLines;
		TheFile.seekg(0, std::ios_base::end);      //Start at end of file
		do 
		{
			char ch = ' ';                             //Init ch not equal to '\n'
			while (ch != '\n')
			{
				TheFile.seekg(-2, std::ios_base::cur); //Two steps back, this means we will NOT check the last character
				if ((int)TheFile.tellg() <= 0)         //If passed the start of the file,
				{
					TheFile.seekg(0);                  //this is the start of the file
					break;
				}
				TheFile.get(ch);                       //Check the next character
			}
			auto FileStreamPos = TheFile.tellg(); // Save Current Stream Position
			std::string TheLine;
			std::getline(TheFile, TheLine);
			auto pos = TheLine.find("\"date\"");
			if (pos != std::string::npos)
			{
				// HACK: I need to clean this up..  
				std::string Value(TheLine.substr(pos));	// value starts at key
				Value.erase(Value.find_first_of(",}"));	// truncate value
				Value.erase(0, Value.find(':'));	// move past key value
				Value.erase(Value.find(':'), 1);	// move past seperator
				Value.erase(Value.find('"'), 1);	// erase leading quote
				Value.erase(Value.find('"'), 1);	// erase trailing quote
				time_t DataTime = ISO8601totime(Value);
				if ((Minutes == 0) && LogLines.empty()) // HACK: Special Case to always accept the last logged value
					LogLines.push(TheLine);
				if ((Minutes * 60.0) < difftime(now, DataTime))	// If this entry is more than Minutes parameter from current time, it's time to stop reading log file.
					break;
				LogLines.push(TheLine);
			}
			TheFile.seekg(FileStreamPos);	// Move Stream position to where it was before reading TheLine
		} while (TheFile.tellg() > 0);	// If we are at the beginning of the file, there's nothing more to do
		TheFile.close();

		if (!LogLines.empty())	// Only return data if we've recieved data in the last 5 minutes
		{
			long long NumElements = LogLines.size();
			double power = 0;
			double voltage = 0;
			long long power_mw = 0;
			long long voltage_mv = 0;
			while (!LogLines.empty())
			{
				std::string TheLine(LogLines.front());
				LogLines.pop();
				auto pos = TheLine.find("\"power\"");
				if (pos != std::string::npos)
				{
					// HACK: I need to clean this up..  
					std::string Value(TheLine.substr(pos));	// value starts at key
					Value.erase(Value.find_first_of(",}"));	// truncate value
					Value.erase(0, Value.find(':'));	// move past key value
					Value.erase(Value.find(':'), 1);	// move past seperator
					power += std::stod(Value.c_str());
				}

				pos = TheLine.find("\"voltage\"");
				if (pos != std::string::npos)
				{
					// HACK: I need to clean this up..  
					std::string Value(TheLine.substr(pos));	// value starts at key
					Value.erase(Value.find_first_of(",}"));	// truncate value
					Value.erase(0, Value.find(':'));	// move past key value
					Value.erase(Value.find(':'), 1);	// move past seperator
					voltage += std::stod(Value.c_str());
				}

				pos = TheLine.find("\"power_mw\"");
				if (pos != std::string::npos)
				{
					// HACK: I need to clean this up..  
					std::string Value(TheLine.substr(pos));	// value starts at key
					Value.erase(Value.find_first_of(",}"));	// truncate value
					Value.erase(0, Value.find(':'));	// move past key value
					Value.erase(Value.find(':'), 1);	// move past seperator
					power_mw += std::stol(Value);
				}

				pos = TheLine.find("\"voltage_mv\"");
				if (pos != std::string::npos)
				{
					// HACK: I need to clean this up..  
					std::string Value(TheLine.substr(pos));	// value starts at key
					Value.erase(Value.find_first_of(",}"));	// truncate value
					Value.erase(0, Value.find(':'));	// move past key value
					Value.erase(Value.find(':'), 1);	// move past seperator
					voltage_mv += std::stol(Value);
				}
			}
			// Initial Averaging of data may have overflow issues that need to be fixed. 
			// For possible solution see https://www.geeksforgeeks.org/compute-average-two-numbers-without-overflow/ 
			// But it would be better to use a combination with https://www.geeksforgeeks.org/average-of-a-stream-of-numbers/
			power /= double(NumElements);
			voltage /= double(NumElements);
			power_mw /= NumElements;
			voltage_mv /= NumElements;

			std::cout << std::dec; // make sure I'm putting things in decimal format
			if (power_mw != 0)
				std::cout << power_mw << std::endl; // current state of the second variable, normally 'outgoing bytes count'
			else
				std::cout << std::fixed << power * 1000.0 << std::endl; // current state of the second variable, normally 'outgoing bytes count'
			if (voltage_mv != 0)
				std::cout << voltage_mv << std::endl; // current state of the first variable, normally 'incoming bytes count'
			else
				std::cout << std::fixed << voltage * 1000.0 << std::endl; // current state of the first variable, normally 'incoming bytes count'
			std::cout << " " << std::endl; // string (in any human readable format), uptime of the target.
			std::cout << DeviceID << std::endl; // string, name of the target.
		}
	}
}
/////////////////////////////////////////////////////////////////////////////
volatile bool bRun = true; // This is declared volatile so that the compiler won't optimize it out of loops later in the code
void SignalHandlerSIGINT(int signal)
{
	bRun = false;
	std::cerr << "***************** SIGINT: Caught Ctrl-C, finishing loop and quitting. *****************" << std::endl;
}
void SignalHandlerSIGHUP(int signal)
{
	bRun = false;
	std::cerr << "***************** SIGHUP: Caught HangUp, finishing loop and quitting. *****************" << std::endl;
}
/////////////////////////////////////////////////////////////////////////////
int ConsoleVerbosity = 1;
int LogFileTime = 60;
int MinutesAverage = 5;
static void usage(int argc, char **argv)
{
	std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
	std::cout << "  " << ProgramVersionString << std::endl;
	std::cout << "  Options:" << std::endl;
	std::cout << "    -h | --help          Print this message" << std::endl;
	std::cout << "    -l | --log name      Logging Directory [" << LogDirectory << "]" << std::endl;
	std::cout << "    -t | --time seconds  time between log file writes [" << LogFileTime << "]" << std::endl;
	std::cout << "    -v | --verbose level stdout verbosity level [" << ConsoleVerbosity << "]" << std::endl;
	std::cout << "    -m | --mrtg 8006D28F7D6C1FC75E7254E4D10B1D1219A9B81D Get last value for this deviceId" << std::endl;
	std::cout << "    -a | --average minutes [" << MinutesAverage << "]" << std::endl;
	std::cout << std::endl;
}
static const char short_options[] = "hl:t:v:m:";
static const struct option long_options[] = {
		{ "help",   no_argument,       NULL, 'h' },
		{ "log",    required_argument, NULL, 'l' },
		{ "time",   required_argument, NULL, 't' },
		{ "verbose",required_argument, NULL, 'v' },
		{ "mrtg",   required_argument, NULL, 'm' },
		{ "average",required_argument, NULL, 'a' },
		{ 0, 0, 0, 0 }
};
/////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
	///////////////////////////////////////////////////////////////////////////////////////////////
	std::string MRTGAddress;
	for (;;)
	{
		int idx;
		int c = getopt_long(argc, argv, short_options, long_options, &idx);
		if (-1 == c)
			break;
		switch (c)
		{
		case 0: /* getopt_long() flag */
			break;
		case 'h':
			usage(argc, argv);
			exit(EXIT_SUCCESS);
		case 'l':
			LogDirectory = optarg;
			break;
		case 't':
			try { LogFileTime = std::stoi(optarg); }
			catch (const std::invalid_argument& ia) { std::cerr << "Invalid argument: " << ia.what() << std::endl; exit(EXIT_FAILURE); }
			catch (const std::out_of_range& oor) { std::cerr << "Out of Range error: " << oor.what() << std::endl; exit(EXIT_FAILURE); }
			break;
		case 'v':
			try { ConsoleVerbosity = std::stoi(optarg); }
			catch (const std::invalid_argument& ia) { std::cerr << "Invalid argument: " << ia.what() << std::endl; exit(EXIT_FAILURE); }
			catch (const std::out_of_range& oor) { std::cerr << "Out of Range error: " << oor.what() << std::endl; exit(EXIT_FAILURE); }
			break;
		case 'm':
			MRTGAddress = std::string(optarg);
			break;
		case 'a':
			try { MinutesAverage = std::stoi(optarg); }
			catch (const std::invalid_argument& ia) { std::cerr << "Invalid argument: " << ia.what() << std::endl; exit(EXIT_FAILURE); }
			catch (const std::out_of_range& oor) { std::cerr << "Out of Range error: " << oor.what() << std::endl; exit(EXIT_FAILURE); }
			break;
		default:
			usage(argc, argv);
			exit(EXIT_FAILURE);
		}
	}
	///////////////////////////////////////////////////////////////////////////////////////////////
	if (!MRTGAddress.empty())
	{
		GetMRTGOutput(MRTGAddress, MinutesAverage);
		exit(EXIT_SUCCESS);
	}
	///////////////////////////////////////////////////////////////////////////////////////////////
	if (ConsoleVerbosity > 0)
	{
		std::cout << "[" << getTimeISO8601() << "] " << ProgramVersionString << std::endl;
	}
	else 
		std::cerr << ProgramVersionString << " (starting)" << std::endl;
	///////////////////////////////////////////////////////////////////////////////////////////////
	// Set up CTR-C signal handler
	typedef void(*SignalHandlerPointer)(int);
	SignalHandlerPointer previousHandlerSIGINT = signal(SIGINT, SignalHandlerSIGINT);	// Install CTR-C signal handler
	SignalHandlerPointer previousHandlerSIGHUP = signal(SIGHUP, SignalHandlerSIGHUP);	// Install Hangup signal handler

	// Set up a listening socket on UDP Port 9999
	int ServerListenSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (ServerListenSocket != -1)
	{
		struct sockaddr_in saServerListen;
		memset(&saServerListen, 0, sizeof saServerListen); // make sure the struct is empty
		saServerListen.sin_family = AF_INET;
		saServerListen.sin_addr.s_addr = INADDR_ANY;
		//saServerListen.sin_port = htons(9999);	// Port number
		saServerListen.sin_port = htons(0);		// Any Port number
		if (-1 == bind(ServerListenSocket,		// Socket descriptor
				(sockaddr*)&saServerListen,		// Address to bind to
				sizeof(struct sockaddr_in)		// Size of address
		))
		{
			std::cerr << "[" << getTimeISO8601() << "] bind returned -1 indicating failure" << std::endl;
			close(ServerListenSocket);
			ServerListenSocket = -1;
		}
		else
		{
			int currentFlags = fcntl(ServerListenSocket, F_GETFL);
			currentFlags |= O_NONBLOCK;
			fcntl(ServerListenSocket, F_SETFL, currentFlags);
			// Because I'm using the same socket to send out my broadcast messages, I'm setting it to that here
			int bBroadcastSocket = 1; // TRUE
			setsockopt(ServerListenSocket, SOL_SOCKET, SO_BROADCAST, (const char *)&bBroadcastSocket, sizeof(bBroadcastSocket));
		}
	}
	
	time_t CurrentTime;
	time(&CurrentTime);
	time_t LastQueryTime = 0;
	time_t LastBroadcastTime = 0;
	time_t LastLogTime = 0;
	time_t DisplayTime = 0;
	std::vector<struct sockaddr> BroadcastAddresses;
	std::map<CKasaClient, std::queue<std::string>> KasaClients;

	// Loop until we get a Ctrl-C
	while (bRun)
	{
		time(&CurrentTime);
		if (ServerListenSocket != -1)
		{
			// If we are listening for UDP messages on port 9999, we want to broadcast the fact.
			//static std::vector<in_addr> BroadcastAddresses;
			// Fill the list of proper broadcast addresses
			if (BroadcastAddresses.empty())
			{
				struct ifaddrs *ifaddr = NULL;
				if (getifaddrs(&ifaddr) != -1)
				{
					for (auto ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
					{
						if (ifa->ifa_addr != NULL)
						{
							int family = ifa->ifa_addr->sa_family;
							if (ConsoleVerbosity > 0)
							{
								std::cout << ifa->ifa_name;
								std::cout << "\t" << ((family == AF_PACKET) ? "AF_PACKET" : (family == AF_INET) ? "AF_INET" : (family == AF_INET6) ? "AF_INET6" : "???");
								std::cout << " (" << family << ")";
							}
							if (family == AF_INET || family == AF_INET6)
							{
								char host[NI_MAXHOST] = { 0 };
								if (0 == getnameinfo(ifa->ifa_addr,
									(family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
									host, NI_MAXHOST,
									NULL, 0, NI_NUMERICHOST))
								{
									if (ConsoleVerbosity > 0)
										std::cout << "\taddress: " << host;
								}
								host[0] =  0;
								if (0 == getnameinfo(ifa->ifa_ifu.ifu_broadaddr,
									(family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
									host, NI_MAXHOST,
									NULL, 0, NI_NUMERICHOST))
								{
									if (ConsoleVerbosity > 0)
										std::cout << "\tbroadcast: " << host;
									BroadcastAddresses.push_back(*ifa->ifa_ifu.ifu_broadaddr);
								}

								//if (ifa->ifa_flags & IFF_BROADCAST )
								//in_addr baddr = ifa->ifa_ifu.ifu_broadaddr;
								//std::cout << inet_ntoa(baddr) << std::endl;
								//BroadcastAddresses.push_back(baddr);
							}
							if (ConsoleVerbosity > 0)
								std::cout << std::endl;
							//else if (family == AF_PACKET && ifa->ifa_data != NULL) 
							//{
							//	struct rtnl_link_stats *stats = (struct rtnl_link_stats *) ifa->ifa_data;
							//	std::cout << "\t\ttx_packets = " << stats->tx_packets;
							//	std::cout << " rx_packets = " << stats;
							//	printf("\t\ttx_packets = %10u; rx_packets = %10u\n"
							//		"\t\ttx_bytes   = %10u; rx_bytes   = %10u\n",
							//		stats->tx_packets, stats->rx_packets,
							//		stats->tx_bytes, stats->rx_bytes);
							//}
						}
					}
				}
				freeifaddrs(ifaddr);

				if (BroadcastAddresses.empty())
				{
					// https://beej.us/guide/bgnet/html/#structs
					struct sockaddr sa;
					struct sockaddr_in * sa4 = (struct sockaddr_in *)&sa;
					memset(&sa, '\0', sizeof(sockaddr));
					sa4->sin_family = AF_INET;
					if (1 == inet_pton(AF_INET, "255.255.255.255", &(sa4->sin_addr)))
						BroadcastAddresses.push_back(sa);
				}
			}

			// periodically brodcast a UDP Query
			if (difftime(CurrentTime, LastBroadcastTime) > 299)
			{
				LastBroadcastTime = CurrentTime;
				//for each (in_addr Address in BroadcastAddresses)
				// Broadcast our message
				for (auto Address : BroadcastAddresses)
				{
					if (Address.sa_family == AF_INET)
					{
						const std::string KasaSysinfo("{\"system\":{\"get_sysinfo\":{}}}");	// Get System Info (Software & Hardware Versions, MAC, deviceID, hwID etc.)
						auto bufferlen = KasaSysinfo.length();
						uint8_t buffer[256] = { 0 };
						KasaEncrypt(KasaSysinfo, buffer);
						struct sockaddr_in *sin = (struct sockaddr_in *) &Address;
						struct sockaddr_in saBroadCast;
						saBroadCast.sin_family = AF_INET;
						saBroadCast.sin_addr = sin->sin_addr;
						saBroadCast.sin_port = htons(9999);	// Port number
						sendto(ServerListenSocket,			// Socket
							buffer,							// Data buffer
							bufferlen,						// Length of data
							0,								// Flags
							(const struct sockaddr *)&saBroadCast,		// Server address
							sizeof(struct sockaddr));		// Length of address
					}
				}
			}
			// Recieve any reponses
			uint8_t szBuf[8192];
			struct sockaddr sa;
			socklen_t sa_len = sizeof(struct sockaddr);
			ssize_t nRet = 0;
			// Always check for UDP Messages
			while ((nRet = recvfrom(ServerListenSocket, szBuf, sizeof(szBuf), 0, &sa, &sa_len)) > 0)
			{
				std::string ClientResponse;
				KasaDecrypt(nRet, szBuf, ClientResponse);
				char ClientHostname[INET6_ADDRSTRLEN] = { 0 };
				if (sa.sa_family == AF_INET)
				{
					struct sockaddr_in * foo = (struct sockaddr_in *)&sa;
					inet_ntop(sa.sa_family, &(foo->sin_addr), ClientHostname, INET6_ADDRSTRLEN);
				}
				else if (sa.sa_family == AF_INET6)
				{
					struct sockaddr_in6 * foo = (struct sockaddr_in6 *)&sa;
					inet_ntop(sa.sa_family, &(foo->sin6_addr), ClientHostname, INET6_ADDRSTRLEN);
				}
				if (ConsoleVerbosity > 0)
					std::cout << "[" << getTimeISO8601() << "] client (" << ClientHostname << ") says \"" << ClientResponse << "\"" << std::endl;
				if (ClientResponse.find("\"feature\":\"TIM:ENE\"") != std::string::npos)
				{
					// Then I want to add the device to my list to be polled for energy usage
					CKasaClient NewClient;
					NewClient.address.sa_family = sa.sa_family;
					for (long unsigned int index = 0; index < sizeof(NewClient.address.sa_data); index++)
						NewClient.address.sa_data[index] = sa.sa_data[index];
					NewClient.date = CurrentTime;
					NewClient.information = ClientResponse;
					std::queue<std::string> foo;
					auto ret = KasaClients.insert(std::pair<CKasaClient, std::queue<std::string>>(NewClient, foo));
					if (ConsoleVerbosity > 0)
						if (ret.first->second.empty())
							std::cout << "[" << getTimeISO8601() << "] adding (" << ClientHostname << ")" << std::endl;
					if (ClientResponse.find("\"children\":[") != std::string::npos)
					{
						const std::string ssParentID(NewClient.GetDeviceID());
						//here we need to parse the client request and add a new map entry for each "id"
						std::string ssChildren(ClientResponse);
						ssChildren.erase(0, ssChildren.find("\"children\":["));
						ssChildren.erase(0, ssChildren.find("[")+1);
						ssChildren.erase(ssChildren.find("]"));
						std::string ssChild;
						int count = 0;						
						for (auto pos = ssChildren.begin(); pos != ssChildren.end(); pos++)
						{
							if (!((count == 0) && (*pos == ',')) && (*pos != '\\'))
							{
								if (*pos == '{')
									count++;
								ssChild += *pos;
								if (*pos == '}')
									count--;
								if (count == 0)
								{
									ssChild.insert(ssChild.find("id\":\"")+5, ssParentID);
									NewClient.information = ssChild;
									ret = KasaClients.insert(std::pair<CKasaClient, std::queue<std::string>>(NewClient, foo));
									if (ConsoleVerbosity > 0)
										if (ret.first->second.empty())
											std::cout << "[" << getTimeISO8601() << "] adding (" << ClientHostname << ")" << ssChild << std::endl;
									ssChild.clear();	// http://www.cplusplus.com/reference/string/basic_string/clear/
								}
							}
						}
					}
				}
			}
		}

		if (difftime(CurrentTime, LastQueryTime) > 9) // only do this stuff every so often.
		{
			LastQueryTime = CurrentTime;

			for (auto it = KasaClients.begin(); it != KasaClients.end(); ++it)
			{
				auto Client = it->first;
				char ClientHostname[INET6_ADDRSTRLEN] = { 0 };
				if (Client.address.sa_family == AF_INET)
				{
					struct sockaddr_in * foo = (struct sockaddr_in *)&Client.address;
					inet_ntop(Client.address.sa_family, &(foo->sin_addr), ClientHostname, INET6_ADDRSTRLEN);
				}
				else if (Client.address.sa_family == AF_INET6)
				{
					struct sockaddr_in6 * foo = (struct sockaddr_in6 *)&Client.address;
					inet_ntop(Client.address.sa_family, &(foo->sin6_addr), ClientHostname, INET6_ADDRSTRLEN);
				}
				struct addrinfo hints;
				/* Obtain address(es) matching host/port */
				memset(&hints, 0, sizeof(struct addrinfo));
				hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
				hints.ai_socktype = SOCK_STREAM;
				hints.ai_protocol = IPPROTO_TCP;
				struct addrinfo *result;
				if (getaddrinfo(ClientHostname, "9999", &hints, &result) == 0)
				{
					for (auto rp = result; rp != NULL; rp = rp->ai_next)
					{
						int theSocket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
						if (theSocket != -1)
						{
							if (connect(theSocket, rp->ai_addr, rp->ai_addrlen) != -1)
							{	// Success
								//char NetNameBuf[100] = { 0 };
								//inet_ntop(rp->ai_addr->sa_family, rp->ai_addr->sa_data, NetNameBuf, sizeof(NetNameBuf));
								//std::cout << "[" << getTimeISO8601() << "] Connected to: " << NetNameBuf << std::endl;
								char bufHostName[255] = { 0 };
								char bufService[255] = { 0 };
								getnameinfo(rp->ai_addr, rp->ai_addrlen, bufHostName, sizeof(bufHostName), bufService, sizeof(bufService), 0);
								if (ConsoleVerbosity > 0)
									std::cout << "[" << getTimeISO8601() << "] [" << bufHostName << ":" << bufService << "]";
								std::string ssRequest("{\"emeter\":{\"get_realtime\":{}}}");
								// If we are a child instead of a top level device, we have an "id" instead of a "deviceId" and need to format the request with context data
								if ((Client.information.find("\"deviceId\"") == std::string::npos) && 
									(Client.information.find("\"id\"") != std::string::npos))
								{
									// Need to build string in the format of: '{"emeter":{"get_realtime":{}},"context":{"child_ids":["8006842B55612405D20D69504A3F43DA1B2A969406"]}}'
									ssRequest = "{\"emeter\":{\"get_realtime\":{}},\"context\":{\"child_ids\":[\"";
									ssRequest += Client.GetDeviceID();
									ssRequest += "\"]}}";
								}
								uint8_t OutBuffer[1024 * 2] = { 0 };
								KasaEncrypt(ssRequest, OutBuffer + sizeof(uint32_t));
								uint32_t * OutBufferLen = (uint32_t *)OutBuffer;
								*OutBufferLen = htonl(ssRequest.length());
								size_t bufLen = ssRequest.length() + sizeof(uint32_t);
								ssize_t nRet = send(theSocket, OutBuffer, bufLen, 0);
								//if ((ConsoleVerbosity > 0) && (nRet < bufLen))
								//	std::cout << " not everything was sent in one go!";
								if (ConsoleVerbosity > 0)
									std::cout << " (" << nRet << ")=> " << ssRequest;
								if (nRet == bufLen)
								{
									uint8_t InBuffer[1024 * 2] = { 0 };
									nRet = recv(theSocket, InBuffer, sizeof(InBuffer), 0);
									if (nRet > 0)
									{
										uint32_t * InBufferLen = (uint32_t *)InBuffer;
										size_t datasize = ntohl(*InBufferLen);
										if (nRet == (datasize + sizeof(uint32_t)))
										{
											std::string Response;
											KasaDecrypt(nRet - sizeof(uint32_t), InBuffer + sizeof(uint32_t), Response);
											std::ostringstream LogLine;
											LogLine << "{\"date\":\"" << timeToExcelDate(CurrentTime) << "\",";
											LogLine << "\"deviceId\":\"" << Client.GetDeviceID() << "\",";
											LogLine << Response << "}";
											it->second.push(LogLine.str());
											if (ConsoleVerbosity > 0)
												std::cout << " <=(" << nRet << ") " << Response;
										}
										else
											bRun = false; // This is a hack. I'm exiting the program on this issue and counting on systemd to restart me
									}
									//else if (it->second.empty())
									//{
									//	// If the device didn't respond, and we don't have any responses in the queue, let's remove it from the map.
									//	if (ConsoleVerbosity > 0)
									//		std::cout << "  No Response. Removing from map.";
									//	KasaClients.erase(it);
									//}
								}
								else
									bRun = false; // This is a hack. I'm exiting the program on this issue and counting on systemd to restart me
								if (ConsoleVerbosity > 0)
									std::cout << std::endl;
							}
							close(theSocket);
						}
					}
					freeaddrinfo(result);           /* No longer needed */
				}
				else
				{
					std::cerr << "[" << getTimeISO8601() << "] Error getaddrinfo " << std::endl;
					//fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
				}
			}
		}

		if (difftime(CurrentTime, LastLogTime) > LogFileTime) // only do this stuff every so often.
		{
			LastLogTime = CurrentTime;
			GenerateLogFile(KasaClients);
		}
			
		usleep(100); // sleep for 100 microseconds (0.1 ms)
		if (ConsoleVerbosity > 0)
			if (difftime(CurrentTime, DisplayTime) > 0) // update display if it's been over a second
		{
			DisplayTime = CurrentTime;
			std::cout << "[" << getTimeISO8601() << "]\r";
			std::cout.flush();
		}

	}

	GenerateLogFile(KasaClients);

	if (ServerListenSocket != -1)
	{
		close(ServerListenSocket);
	}

	signal(SIGHUP, previousHandlerSIGHUP);	// Restore original Hangup signal handler
	signal(SIGINT, previousHandlerSIGINT);	// Restore original Ctrl-C signal handler
	std::cerr << ProgramVersionString << " (exiting)" << std::endl;
	return 0;
}