#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <iostream>

using namespace std;

// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#ifdef DEBUG
#define errmsg(x, y)    printf(#x " failed with error: %d\n", (y))
#else
#define errmsg(x, y)    
#endif

#define PORT_STR "21817"

bool bExitChat = false; // global flag
HANDLE hExitChatMutex;

typedef struct
{
    char chatname[24];
    SOCKET ConnectSocket;
    WSADATA wsaData;
    HANDLE hRecvWorker;
    struct addrinfo *addr;
    int port;
} AppData;

typedef void (*SignalHandlerPointer)(int);

void SignalHandler(int sig)
{
    WaitForSingleObject(hExitChatMutex, INFINITE);
    bExitChat = true;
    ReleaseMutex(hExitChatMutex);
    // reinstall SIGINT handler
    signal(sig, SignalHandler);
}

DWORD WINAPI RecvWorkerRoutine(LPVOID param) {
    int iResult;
    AppData *pAData = static_cast<AppData*>(param);
    char recvbuf[512];
    int recvbuflen = 512;

    while (true) {
        iResult = recv(pAData->ConnectSocket, recvbuf, recvbuflen, 0);

        WaitForSingleObject(hExitChatMutex, INFINITE);
        if (bExitChat) break;
        ReleaseMutex(hExitChatMutex);

        if ( iResult > 0 ) {
            // append NULL character to recvbuf
            recvbuf[iResult] = '\0';
            cout << "\n" << recvbuf << "\n" << pAData->chatname << ": ";
        }
        else if ( iResult == 0 ) {
            cout << "Connection closed\n";
            break;
        }
        else {
            errmsg(recv, WSAGetLastError());
            break;
        }
    }

    return 0;
}

bool validateArg(char arg[])
{
    if (!arg) return false;

    if (!strcmp(arg, "localhost")) return true;

    if (arg[0] == '.') return false;
    int dots = 0;
    for (int i = 1; i < strlen(arg) - 1; ++i)
    {
        if ( (arg[i] == '.') && (arg[i-1] != '.') && (arg[i+1] != '.') ) ++dots;
    }
    if (dots == 3) return true;

    return false;
}
int __cdecl main(int argc, char **argv) 
{
    AppData ad;
    char *key = "1234"; // TODO crypto
    char sendbuf[512];
    int sendbuflen = 512;
    char recvbuf[512];
    int recvbuflen = 512;
    int iResult;
    int sendlen = 0;

    memset(&ad, 0, sizeof(ad));
    ad.ConnectSocket = INVALID_SOCKET;

    // Validate the parameters
    if ( (argc != 2) || (!validateArg(argv[1])) )
    {
        cout << "Usage: " << argv[0] << " [server]\n";
        cout << "[server] - \"localhost\", IPv4 address\n";

        return 1;
    }

    // Setup SIGINT handler
    SignalHandlerPointer previousHandler;
    previousHandler = signal(SIGINT, SignalHandler);
    
    hExitChatMutex = CreateMutex(NULL, FALSE, NULL);

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &(ad.wsaData));
    if (iResult != 0) {
        errmsg(WSAStartup, iResult);
        return 1;
    }

    // set server port number
    ad.port = atoi(PORT_STR);

    // setup server address
    struct addrinfo hints;
    ZeroMemory( &(hints), sizeof(hints) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    iResult = getaddrinfo(argv[1], PORT_STR, &(hints), &(ad.addr));
    if ( iResult != 0 ) {
        errmsg(getaddrinfo, iResult);
        WSACleanup();
        return 1;
    }

    cout << ":::::::Winsock v2 based chat program:::::::\n";
    cout << "::::::::::::::Client App v1.0::::::::::::::\n\n";
    cout << "Settings\n";
    cout << "-------------------------------------------\n";
    cout << "\tIP Address: " << argv[1] << "\n";
    //cout << "\tPORT: " << ad.port << "\n";
    cout << "-------------------------------------------\n\n";
    cout << "Connecting to server...\n";

    // Attempt to connect to an address until one succeeds
    struct addrinfo *ptr;
    for(ptr = ad.addr; ptr != NULL ; ptr = ptr->ai_next) {

        // Create a SOCKET for connecting to server
        ad.ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, 
            ptr->ai_protocol);
        if (ad.ConnectSocket == INVALID_SOCKET) {
            errmsg(socket, WSAGetLastError());
            WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect( ad.ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            errmsg(connect, iResult);
            closesocket(ad.ConnectSocket);
            ad.ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(ad.addr);

    if (ad.ConnectSocket == INVALID_SOCKET) {
        cout << "Unable to connect to server!\n";
        WSACleanup();
        return 1;
    }

    cout << "Connected to server\n";
    cout << "Authenticating...\n";

    // Send an key
    iResult = send( ad.ConnectSocket, key, (int)strlen(key), 0 );
    if (iResult == SOCKET_ERROR) {
        errmsg(send, WSAGetLastError());
        closesocket(ad.ConnectSocket);
        WSACleanup();
        return 1;
    }
    // wait for response
    iResult = recv( ad.ConnectSocket, recvbuf, recvbuflen, 0);
    if ( iResult > 0 ) {
        // append NULL character to recvbuf
        recvbuf[iResult] = '\0';

            
        if ( !strcmp(recvbuf, "FAIL") )
        {
            cout << "Authentication " << recvbuf << "\n";
            closesocket(ad.ConnectSocket);
            WSACleanup();
            return 1;
        }
        else if ( !strcmp(recvbuf, "OK") )
        {
            cout << "Authentication " << recvbuf << "\n";
        }
        else
        {
            cout << "Warning! Erroneous response from server\n";
        }
    }
    else if ( iResult == 0 ) {
        cout << "Connection closed\n";
        closesocket(ad.ConnectSocket);
        WSACleanup();
        return 1;
    }
    else {
        errmsg(recv, WSAGetLastError());
        closesocket(ad.ConnectSocket);
        WSACleanup();
        return 1;
    }

    while (true)
    {
        WaitForSingleObject(hExitChatMutex, INFINITE);
        if (bExitChat) goto end;
        ReleaseMutex(hExitChatMutex);

        cout << "\nEnter your chat name: ";
        cin >> ad.chatname;
        if (cin.fail())
        {
            cin.clear();
            Sleep(5); // need sometime for signal handler to fire else cin << blocks
        }
        else
        {
            break;
        }
    };

    iResult = send( ad.ConnectSocket, ad.chatname, (int)strlen(ad.chatname), 0 );
    if (iResult == SOCKET_ERROR) {
        errmsg(send, WSAGetLastError());
        closesocket(ad.ConnectSocket);
        WSACleanup();
        return 1;
    }

    cout << "You are online, " << ad.chatname << "\n\n";

    ad.hRecvWorker = CreateThread(NULL, 0, RecvWorkerRoutine, (LPVOID)&ad, 0, NULL);
    // handle error

    cin.sync();
    // send until user exits or server closes the connection
    while (true)
    {
        WaitForSingleObject(hExitChatMutex, INFINITE);
        if (bExitChat) goto end;
        ReleaseMutex(hExitChatMutex);

        cout << ad.chatname << ": ";
        //cin.ignore(); // Ignore next input (= Ctr+C)

        cin.getline(sendbuf, sendbuflen);
        if (cin.fail())
        {
            cin.clear(); // reset cin state
            Sleep(5); // need sometime for signal handler to fire else cin << blocks
        }

        sendlen = strlen(sendbuf);
        if (sendlen != 0)
        {
            iResult = send( ad.ConnectSocket, sendbuf, sendlen, 0 );
            if (iResult == SOCKET_ERROR) {
                errmsg(send, WSAGetLastError());
                closesocket(ad.ConnectSocket);
                WSACleanup();
                return 1;
            }
        }
    };

end:
    iResult = shutdown(ad.ConnectSocket, SD_SEND);
    if (iResult == SOCKET_ERROR) {
        errmsg(shutdown, WSAGetLastError());
        closesocket(ad.ConnectSocket);
        WSACleanup();
        return 1;
    }

    cout << "\n:::::::::Client App will exit now:::::::::\n";

    // cleanup
    CloseHandle(ad.hRecvWorker);
    closesocket(ad.ConnectSocket);
    WSACleanup();
    signal(SIGINT, previousHandler);

    return 0;
}

#undef WIN32_LEAN_AND_MEAN
