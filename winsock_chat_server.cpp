
//TODO  pass parameters while returning from thread to close and shutdown socket
//TODO replace CreateThread with _beginthreadex
//TODO add interrupt signal handler

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

#ifdef DEBUG
#define errmsg(x, y)    printf(#x " failed with error: %d\n", (y))
#else
#define errmsg(x, y)    
#endif

using namespace std;

#define MAX_CLIENTS     5
#define PORT_STR        "21817"

typedef struct
{
    HANDLE hClientWorkers[MAX_CLIENTS];
    SOCKET ClientSockets[MAX_CLIENTS];
    SOCKET ListenSocket;
    struct addrinfo *addr;
    WSADATA wsaData;
    char chatnames[MAX_CLIENTS][24];
    int nClients;
    int port;
} AppData;

typedef struct
{
    AppData *pAData;
    int clientID;
} ThreadData;

bool WINAPI Authenticate(SOCKET client) {
    int ret;
    char recvbuf[12];
    int recvbuflen = 12;
    // TODO setup crypto for key
    char *key = "1234";

    cout << "Authenticating client...\n";

    ret = recv(client, recvbuf, recvbuflen, 0);
    if (ret > 0)
        if (!strncmp(recvbuf, key, ret))
            return true;

    return false;
}

DWORD WINAPI ClientWorkerRoutine(LPVOID param) {
    int ret;
    char recvbuf[512];
    int recvbuflen = 512;
    char sendbuf[512];
    int sendbuflen = 512;
    bool exitChat = false;

    ThreadData *pTData = static_cast<ThreadData*>(param);
    int clientID = pTData->clientID;
    SOCKET clientSocket = pTData->pAData->ClientSockets[clientID];

    //cout << "I am alive" << endl;

    ret = recv(clientSocket, recvbuf, recvbuflen, 0);
    if (ret > 0) {
        strncpy(pTData->pAData->chatnames[clientID], recvbuf, ret);
        pTData->pAData->chatnames[clientID][ret] = '\0';
    }
    else if(ret == 0)
    {
        cout << "Connection closed from client\n";
        goto cleanup;
    }
    else
    {
        errmsg(recv, WSAGetLastError());
        cout << "Client " << pTData->pAData->chatnames[clientID] << " offline\n";
        goto end;
    }

    cout << "Client " << pTData->pAData->chatnames[clientID] << " online\n";

    while (!exitChat) {
        ret = recv(clientSocket, recvbuf, recvbuflen, 0);
        if (ret > 0) {
            recvbuf[ret] = '\0';

            sprintf(sendbuf, "%s: %s", pTData->pAData->chatnames[clientID], recvbuf);
            for(int i = 0; i < pTData->pAData->nClients; i++) {
                if (i == clientID) continue;

                ret = send(pTData->pAData->ClientSockets[i], sendbuf, strlen(sendbuf), 0);
                if (ret == SOCKET_ERROR)
                {
                    errmsg(send, WSAGetLastError());
                    cerr << "Could not send to " << pTData->pAData->chatnames[i]
                         << " with socket ID: " << pTData->pAData->ClientSockets[i] << "\n";
                }
            }
        }
        else if(ret == 0)
        {
            cout << "Connection closed from " << pTData->pAData->chatnames[clientID] << "\n";
            goto cleanup;
        }
        else
        {
            errmsg(recv, WSAGetLastError());
            cout << "Client " << pTData->pAData->chatnames[clientID] << " offline\n";
            goto end;
        }
    }

cleanup:
    // move this code to main thread
    ret = shutdown(clientSocket, SD_SEND);
    if (ret == SOCKET_ERROR)
    {
        errmsg(shutdown, WSAGetLastError());
    }

end:
    closesocket(clientSocket);
    pTData->pAData->ClientSockets[clientID] = INVALID_SOCKET;
    pTData->pAData->nClients--;

    delete pTData;

    return 0;
}

int getFreeClientID(AppData *pAData)
{
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if ( pAData->ClientSockets[i] == INVALID_SOCKET ) return i;
    }
    return -1;
}

int __cdecl main() {
    int ret;
    AppData ad;

    memset(&ad, 0, sizeof(ad));
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        ad.ClientSockets[i] = INVALID_SOCKET;
    }
    ad.ListenSocket = INVALID_SOCKET;

    ret = WSAStartup(MAKEWORD(2,2), &(ad.wsaData));
    if (ret != 0) {
        errmsg(WSAStartup, ret);
        return 1;
    }
  
    // setup server address 
    struct addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    ret = getaddrinfo(NULL, PORT_STR, &hints, &(ad.addr));
    if (ret != 0) {
        errmsg(getaddrinfo, ret);
        WSACleanup();
        return 1;
    }

    // set server port number
    ad.port = atoi(PORT_STR);

    cout << ":::::::Winsock v2 based chat program:::::::\n";
    cout << "::::::::::::::Server App v1.0::::::::::::::\n\n";
    cout << "Settings\n";
    cout << "-------------------------------------------\n";
    cout << "\tIP Address: INADDR_ANY\n";
    cout << "\tPORT: " << ad.port << "\n";
    cout << "-------------------------------------------\n\n";
    cout << "Setting up server...\n";

    ad.ListenSocket = socket(ad.addr->ai_family, ad.addr->ai_socktype, ad.addr->ai_protocol);
    if (ad.ListenSocket == INVALID_SOCKET) {
        errmsg(socket, WSAGetLastError());
        WSACleanup();
        return 1;
    } 

    ret = bind(ad.ListenSocket, ad.addr->ai_addr, (int)ad.addr->ai_addrlen);
    if (ret == SOCKET_ERROR) {
        errmsg(bind, WSAGetLastError());
        closesocket(ad.ListenSocket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(ad.addr);

    ret = listen(ad.ListenSocket, SOMAXCONN);
    if (ret == SOCKET_ERROR) {
        errmsg(listen, WSAGetLastError());
        closesocket(ad.ListenSocket);
        WSACleanup();
        return 1;
    }

    cout << "Server is setup and ready for clients\n";

    while (true)
    {
        if (ad.nClients < MAX_CLIENTS)
        {
            SOCKET client = accept(ad.ListenSocket, NULL, NULL);
            if (client == INVALID_SOCKET)
            {
                errmsg(accept, WSAGetLastError());
                goto cleanup;
            }

            cout << "\nClient found\n";

            if ( !Authenticate(client) ) {
                errmsg(Authenticate, 0);
                cout << "Client validation FAILURE\n";

                if ( (send(client, "FAIL", 4, 0)) == SOCKET_ERROR )
                {
                    errmsg(send, WSAGetLastError());
                }

                closesocket(client);
                continue;
            }
            else
            {
                cout << "Client validation OK\n";

                if ( (send(client, "OK", 2, 0)) == SOCKET_ERROR )
                {
                    errmsg(send, WSAGetLastError());
                    closesocket(client);
                    continue;
                }
            }

            int free_id = getFreeClientID(&ad);

            ad.ClientSockets[free_id] = client;

            ThreadData *pTData = new ThreadData;
            pTData->pAData = &ad;
            pTData->clientID = free_id;
            ad.hClientWorkers[free_id] = CreateThread(NULL, 0, ClientWorkerRoutine, (LPVOID)pTData, 0, NULL);

            ad.nClients++;
            cout << "\n[ " << ad.nClients << " clients are online now ]\n\n";
        }
        else
        {
            cout << "Warning! Server will not accept clients anymore\n";
            cout << "\n[ " << MAX_CLIENTS << " clients are already online ]\n" << endl;
        }
    }


    WaitForMultipleObjects(MAX_CLIENTS, ad.hClientWorkers, TRUE, INFINITE);

cleanup:
    cout << "\n:::::::::Client App will exit now:::::::::\n";

    for(int i = 0; i < MAX_CLIENTS; ++i)
    {
        CloseHandle(ad.hClientWorkers[i]);
    }
    closesocket(ad.ListenSocket);
    WSACleanup();

    return 0;
}

#undef WIN32_LEAN_AND_MEAN
