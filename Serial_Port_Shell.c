#include <windows.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <stdint.h>
#include <unistd.h>
#include <conio.h>

#define MAXNumOfSerialPort 10
#define MAXDeviceNameLen 6
#define MAXInterfaceNameLen 50

HANDLE SerialPortHandler;
HANDLE SerialPortWriteHandler;
HANDLE StdInHandler;
HANDLE StdOutHandler;
OVERLAPPED SerialPortReadOverlapped = {0};
OVERLAPPED SerialPortWriteOverlapped = {0};
DWORD DefaultStdInMode;
DWORD DefaultStdOutMode;

bool SerialPortNameflag = false;
bool Restartflag = false;
bool OnlyPrintSerialPortList = false;
struct SerialPortsListItem
{
    char DeviceName[MAXDeviceNameLen];
    char InterfaceName[MAXInterfaceNameLen];
};

struct SerialPortsList
{
    unsigned short TheNumOfSerialPort;
    struct SerialPortsListItem SerialPorts[MAXNumOfSerialPort];  
};

enum ParityEnum 
{
    NoParity = 0,
    OddParity,
    EvenParity,
    MarkParity,
    SpaceParity
};
char ParityStr[5][6] = {"NO","Odd","Even","Mark","Space"};

enum StopBitsEnum
{
    StopBits_1 = 0,
    StopBits_1_5,
    StopBits_2
};
char StopBitsStr[3][4] = {"1","1.5","2"};

struct SerialPortToOpen 
{
    struct SerialPortsListItem SerialPort;
    uint32_t BaudRate;
    enum ParityEnum Parity;
    uint8_t DataBits;
    enum StopBitsEnum StopBits;
};

struct SerialPortsList SerialPortsList = {0};
struct SerialPortToOpen SerialPort = {{"",""},115200,NoParity,8,StopBits_1};

bool CheckParameters(int argc, char *argv[]) 
{
    if (argc == 2 && !stricmp(argv[1], "getportlist")) {
        OnlyPrintSerialPortList = true;
        return true;
    }
    if (argc > 6) {
        return false;
    } else {
        for (int i=1; i<argc; i++) {
            if (strstr(argv[i],"-B=") != NULL) {
                SerialPort.BaudRate = atoi(&argv[i][3]);
            } else if (strstr(argv[i],"-P=") != NULL) {
                int j;
                for (j=0; j<5; j++) {
                    if (!stricmp(&argv[i][3],ParityStr[j])) {
                        SerialPort.Parity = j;
                        break;
                    }
                }
                if (j == 5) {
                    return false;
                }
            } else if (strstr(argv[i],"-D=") != NULL) {
                SerialPort.DataBits = atoi(&argv[i][3]);
            } else if (strstr(argv[i],"-S=") != NULL) {
                int j;
                for (j=0; j<3; j++) {
                    if (!strcmp(&argv[i][3],StopBitsStr[j])) {
                        SerialPort.StopBits = j;
                        break;
                    }
                }
                if (j == 3) {
                    return false;                    
                }
            } else if (strstr(argv[i],"-C=")) {
                strcpy(SerialPort.SerialPort.DeviceName,&argv[i][3]);
                SerialPortNameflag = true;
            } else {
                return false;
            }
        } 
    }
    return true;
}

bool GetSerialPortsInfoFromKey()
{
    HKEY hKey;
	LSTATUS status;
    //打开串口注册表
    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("HARDWARE\\DEVICEMAP\\SERIALCOMM"), REG_OPTION_OPEN_LINK, KEY_READ, &hKey);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    DWORD numValues, maxValueNameLen, maxValueLen;
    //获取串口信息（端口号字符串长度和有多少串口等)
	status = RegQueryInfoKey(hKey, NULL, NULL, NULL, NULL, NULL, NULL, &numValues, &maxValueNameLen, &maxValueLen, NULL, NULL);
    if (status != ERROR_SUCCESS) {
		RegCloseKey(hKey);
        return false;
    }
    if (numValues <= 0) {
		RegCloseKey(hKey);
		return false;
	}
    maxValueLen++;
	maxValueNameLen++;
	char* DeviceName = malloc(sizeof(char)*maxValueLen);
	char* InterfaceName = malloc(sizeof(char)*maxValueNameLen);
    //遍历串口，登记串口号和接口名
    SerialPortsList.TheNumOfSerialPort = 0;
    for (DWORD idx = 0; idx < numValues; idx++) {
		DWORD ValueNameLen = maxValueNameLen;
		DWORD ValueLen = maxValueLen;
		status = RegEnumValue(hKey, idx, InterfaceName, &ValueNameLen, NULL, NULL, (LPBYTE)DeviceName, &ValueLen);
		if (status == ERROR_SUCCESS) {
            strcpy(SerialPortsList.SerialPorts[idx].DeviceName,DeviceName);
            strcpy(SerialPortsList.SerialPorts[idx].InterfaceName,InterfaceName);
            SerialPortsList.TheNumOfSerialPort++;
		}
	}
    free(DeviceName);
    free(InterfaceName);
    RegCloseKey(hKey);
    return true;
}

bool CheckSerialPortName(char *SerialPortName)
{
    for (int i=0; i<SerialPortsList.TheNumOfSerialPort; i++) {
        if (!stricmp(SerialPortName,SerialPortsList.SerialPorts[i].DeviceName)) {
            return true;
        }
    }
    return false;
}

bool OpenSerialPort(char *PortName)//打开串口
{
	SerialPortHandler = CreateFile(PortName, //串口号
					   GENERIC_READ | GENERIC_WRITE, //允许读写
					   0, //通讯设备必须以独占方式打开
					   NULL, //无安全属性
					   OPEN_EXISTING, //通讯设备已存在
					   FILE_FLAG_OVERLAPPED, //异步I/O
					   NULL); //通讯设备不能用模板打开

	if (SerialPortHandler == INVALID_HANDLE_VALUE)
	{
		CloseHandle(SerialPortHandler);
		return false;
	}
	else
		return true;
}

bool ConfigureSerialPort(void) 
{
    DCB SerialPortDCB;
    memset(&SerialPortDCB,0,sizeof(SerialPortDCB));
    if (!GetCommState(SerialPortHandler,&SerialPortDCB)) {
        return false;
    }
    SerialPortDCB.DCBlength       = sizeof(SerialPortDCB);
    SerialPortDCB.BaudRate        = SerialPort.BaudRate;
    SerialPortDCB.Parity          = SerialPort.Parity;
    SerialPortDCB.fParity         = 0;
    SerialPortDCB.StopBits        = SerialPort.StopBits;
    SerialPortDCB.ByteSize        = SerialPort.DataBits;
    SerialPortDCB.fOutxCtsFlow    = 0;
    SerialPortDCB.fOutxDsrFlow    = 0;
    SerialPortDCB.fDtrControl     = DTR_CONTROL_DISABLE;
    SerialPortDCB.fDsrSensitivity = 0;
    SerialPortDCB.fRtsControl     = RTS_CONTROL_DISABLE;
    SerialPortDCB.fOutX           = 0;
    SerialPortDCB.fInX            = 0;
    /* ----------------- misc parameters ----- */
    SerialPortDCB.fErrorChar      = 0;
    SerialPortDCB.fBinary         = 1;
    SerialPortDCB.fNull           = 0;
    SerialPortDCB.fAbortOnError   = 0;
    SerialPortDCB.wReserved       = 0;
    SerialPortDCB.XonLim          = 2;
    SerialPortDCB.XoffLim         = 4;
    SerialPortDCB.XonChar         = 0x13;
    SerialPortDCB.XoffChar        = 0x19;
    SerialPortDCB.EvtChar         = 0;
    if(!SetCommState(SerialPortHandler,&SerialPortDCB)) {
        return false;
    }
    //丢弃指定通信资源的输出或输入缓冲区中的所有字符,终止对资源的等待读取或写入操作
    PurgeComm(SerialPortHandler, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
	//指定要监视通信设备的一组事件即收到一个字符并放在输入缓冲区中
    SetCommMask(SerialPortHandler, EV_RXCHAR);
    //初始化指定通信设备的通信参数，输入/输出缓冲区的大小
	SetupComm(SerialPortHandler, 1, 1);
    SerialPortReadOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (SerialPortReadOverlapped.hEvent == NULL) {
        CloseHandle(SerialPortHandler);
        return false;
    }
    SerialPortWriteOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (SerialPortWriteOverlapped.hEvent == NULL) {
        CloseHandle(SerialPortHandler);
        return false;
    }
    return true;
}
//读串口线程的回调函数
DWORD WINAPI ReceiveChar(_In_ LPVOID lpParameter) 
{
    char buf;
    DWORD nBytesRead;
    for (;;) {
        //将指定的事件对象的状态设置为非信号
        ResetEvent(SerialPortReadOverlapped.hEvent);
        if (!ReadFile(SerialPortHandler, &buf, 1, &nBytesRead, &SerialPortReadOverlapped)) {
			if (GetLastError() == ERROR_IO_PENDING) {
				if (!GetOverlappedResult(SerialPortHandler, &SerialPortReadOverlapped, &nBytesRead, TRUE)) {
					break;
				}
			}
		}
        if (nBytesRead > 0 && buf != '\0') {
            DWORD lpNumberOfBytesWritten;
			WriteFile(StdOutHandler, &buf, nBytesRead, &lpNumberOfBytesWritten,NULL);
		}
    }
    return 0;
}

void WriteChar(void) 
{
    char Data[4];
    DWORD DataLen;
    DWORD nBytesWritten;
    for (;;) {
        ReadConsole(StdInHandler, Data, 4, &DataLen, NULL);
        if ((Data[0] == 3) || (Data[0] == 4)) {
            break;
        } else if (Data[0] == 18) {
            Restartflag = true;
            break;
        }
        ResetEvent(SerialPortWriteOverlapped.hEvent);
        if (!WriteFile(SerialPortHandler, Data, DataLen, &nBytesWritten, &SerialPortWriteOverlapped)) {
			if (GetLastError() == ERROR_IO_PENDING) {
				if (!GetOverlappedResult(SerialPortHandler, &SerialPortWriteOverlapped, &nBytesWritten, TRUE)) {
					break;
				}
			}
		}
        if (nBytesWritten == 0) {
            printf("\nSend data to serial port error, the serial port detached?\n");
            break;
        }
    }
    CloseHandle(SerialPortWriteOverlapped.hEvent);
}

void ConfigureConsole(void) 
{
    DWORD mode;
    StdInHandler = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(StdInHandler, &mode);
    DefaultStdInMode = mode;
	mode &= ~ENABLE_PROCESSED_INPUT;
	mode &= ~ENABLE_LINE_INPUT;
	mode |= 0x0200;
	SetConsoleMode(StdInHandler, mode);
    
    StdOutHandler = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleMode(StdOutHandler, &mode);
    DefaultStdOutMode = mode;
	mode |= 0x0004;
	SetConsoleMode(StdOutHandler, mode);
}

void ConsoleReInit(void) 
{
    StdInHandler = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(StdInHandler, DefaultStdInMode);
    StdOutHandler = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleMode(StdOutHandler, DefaultStdOutMode);
}

int main(int argc, char *argv[])
{
    if (!CheckParameters(argc,argv)) {
        printf("There are errors with parameters!\n");
        return -1;
    }
    restart:
    if (!GetSerialPortsInfoFromKey()) {
        printf("Error to Get Serial Ports List\n");
        return -2;
    }
    if (SerialPortsList.TheNumOfSerialPort == 0) {
        printf("No Serial Port Exist!\n");
        return -3;
    } 
    if (OnlyPrintSerialPortList) {
        for (int i=0; i<SerialPortsList.TheNumOfSerialPort; i++) {
            printf("%s\t",SerialPortsList.SerialPorts[i].DeviceName);
        }
        return 0;
    }
    if (SerialPortsList.TheNumOfSerialPort > 1 && !SerialPortNameflag) {
        printf("There are more than one Serial Ports:\n");
        for (int i=0; i<SerialPortsList.TheNumOfSerialPort; i++) {
            printf("%s ",SerialPortsList.SerialPorts[i].DeviceName);
        }
        printf("\nPlease choose one:");
        scanf("%s",SerialPort.SerialPort.DeviceName);
        if (!CheckSerialPortName(SerialPort.SerialPort.DeviceName)) {
            printf("Invaild serial port name:%s\n",SerialPort.SerialPort.DeviceName);
            return -4;
        }
    } else if (!SerialPortNameflag) {
        strcpy(SerialPort.SerialPort.DeviceName,SerialPortsList.SerialPorts[0].DeviceName);
    }
    if (!OpenSerialPort(SerialPort.SerialPort.DeviceName)) {
        printf("Can't open Serial Port of %s!\n",SerialPort.SerialPort.DeviceName);
		return -5;
    }
    if (!ConfigureSerialPort()) {
        printf("Parameters configure error!\n");
        CloseHandle(SerialPortHandler);
        return -6;
    }
    ConfigureConsole();
    SerialPortWriteHandler = CreateThread(NULL, 0, &ReceiveChar, NULL, 0, NULL);
    if (SerialPortWriteHandler == NULL) {
        printf("Create Serial Port Write Char Thread Error!\n");
        CloseHandle(SerialPortHandler);
        CloseHandle(SerialPortWriteHandler);
        return -7;
    }
    printf("Success to launch and open %s\n",SerialPort.SerialPort.DeviceName);
    printf("Serial Port Parameters: BaudRate = %d Parity = %s DataBits = %d StopBits = %s \n",
    SerialPort.BaudRate,ParityStr[SerialPort.Parity],SerialPort.DataBits,StopBitsStr[SerialPort.StopBits]);
    WriteChar();
    CancelIo(SerialPortHandler);
    CloseHandle(SerialPortReadOverlapped.hEvent);
    //WaitForSingleObject(SerialPortWriteHandler, INFINITE);
    CloseHandle(SerialPortHandler);
    CloseHandle(SerialPortWriteHandler);
    ConsoleReInit();
    if (Restartflag) {
        Restartflag = false;
        printf("\nSerial Port Restart:\n");
        goto restart;
    }
    return 0;
}
