#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include<vector>
#include<thread>
#include<iostream>
#include<memory>
// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define ThreadNo 2
#define DEFAULT_BUFLEN 2560
#define DEFAULT_PORT "9000"
#define DEFAULT_IP "127.0.0.1"
HANDLE eventQueueIOPort, acceptEvent;



std::string HttpResponseContent= "HTTP/1.2 200 OK\r\nContent-Length:2\r\nContent-Type: text/html\r\n\r\nHi";

class SockInformation {
public:
	OVERLAPPED overlapped;
	WSABUF wsaBuf;
	char buffer[DEFAULT_BUFLEN];
	std::string IncompleteBuffer; //黏包用
	SOCKET socket;
	SockInformation(SOCKET messageSocket) {
		ZeroMemory(buffer, DEFAULT_BUFLEN);
		memset(&overlapped, 0, sizeof(OVERLAPPED));

		wsaBuf.len = DEFAULT_BUFLEN;
		wsaBuf.buf = buffer;
		this->socket = messageSocket;
	}
	~SockInformation() {
		printf("closing socket from server\n");
		closesocket(socket);
	}
};

int getHttpRequestDataEndLine(std::string buf)
{
	for (int i = 0; i < buf.size()-4; i++)
	{
		if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
			return i+3;
	}
	return -1;
}

void workerThread_RecvAndSend()
{
	int iResult = 0;
	char* recvbuf = new char[DEFAULT_BUFLEN];
	int recvbuflen = DEFAULT_BUFLEN;
	// No longer need server socket
//closesocket(ListenSocket);
	ULONG_PTR* ipCompletionKey;
	WSAOVERLAPPED* ipOverlap;
	DWORD ipNumberOfBytes;
	OVERLAPPED* overlapped = new OVERLAPPED();
	DWORD flags = 0, recvBytes;
	while (true)
	{
		if (!GetQueuedCompletionStatus(
			eventQueueIOPort,
			&ipNumberOfBytes,
			(PULONG_PTR)&ipCompletionKey,
			&ipOverlap,
			INFINITE))
		{
			//std::cout << "errorCode:" << GetLastError() << " ";
			std::cout << "worker thread "<<std::this_thread::get_id()<<" retrying GetQueuedCompletionStatus\n";
			continue;

		} 
		//如同GetQueuedCompletionStatus,WSAWaitForMultipleEvents 有類似block的效果,但通過WSAWaitForMultipleEvents後需要靠WSAGetOverlappedResult 
		//取得已經被傳入IOCP的資料,最後還需要靠 WSACloseEvent或WSAEnumNetworkEvents對event觸發序號進行reset,
		//如此一來再次進入WSAWaitForMultipleEvents才能維持block的狀態

		auto recvPortData = (SockInformation*)ipCompletionKey;	
		if (ipNumberOfBytes > 0) {
			printf("Bytes received: %d\n", ipNumberOfBytes);
		
			
			//***Http Fragment packet assemble(currently not in use)
			//recvPortData->IncompleteBuffer.append(recvPortData->buffer);
			//int endLineIndex = 0;
			//if( (endLineIndex= getHttpRequestDataEndLine(recvPortData->buffer))<0)				
			//{
			//	DWORD recvBytes;
			//	recvPortData->IncompleteBuffer.append(recvPortData->buffer);
			//	WSARecv(recvPortData->socket, &(recvPortData->wsaBuf), 1, &recvBytes, &flags, &(recvPortData->overlapped), NULL);
			//	continue;
			//}	
			//if(endLineIndex!=ipNumberOfBytes-1)
			//	recvPortData
			//if(endLineIndex...)
			//recvPortData->IncompleteBuffer.clear();
			//int sendResult = send(recvPortData->socket, HttpResponseContent.c_str(), HttpResponseContent.size(), 0); 
			//****


			int sendResult = send(recvPortData->socket, recvPortData->buffer, ipNumberOfBytes, 0);//收到資料馬上回復,沒有asynch需求
			if (sendResult == SOCKET_ERROR) {
				printf("send failed with error: %d\n", WSAGetLastError());
				delete recvPortData;
				continue;

			}
			std::cout << "threadID: " << std::this_thread::get_id() << std::endl;
			std::cout << recvPortData->buffer << std::endl;
			ZeroMemory(recvPortData->buffer, DEFAULT_BUFLEN);
			DWORD recvBytes;
			WSARecv(recvPortData->socket, &(recvPortData->wsaBuf), 1, &recvBytes, &flags, &(recvPortData->overlapped), NULL);
		}
		else if (ipNumberOfBytes == 0)
		{
			printf("Connection closing...\n");
			delete recvPortData;
			continue;
		}
		else {
			printf("recv failed with error: %d\n", WSAGetLastError());
			delete recvPortData;
			continue;
		}
	}
}

int main()
{
	WSADATA wsaData;
	int sockOprResult;
	SOCKET ListenSocket = INVALID_SOCKET;
	SOCKET ClientSocket = INVALID_SOCKET;
	struct addrinfo* addrInfo_ListenSocket = NULL;
	struct addrinfo hints;

	// int iSendResult;
	char* recvbuf = new char[DEFAULT_BUFLEN];
	int recvbuflen = DEFAULT_BUFLEN;

	// Initialize Winsock
	sockOprResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (sockOprResult != 0) {
		printf("WSAStartup failed with error: %d\n", sockOprResult);
		return 1;
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	sockOprResult = getaddrinfo(DEFAULT_IP, DEFAULT_PORT, &hints, &addrInfo_ListenSocket);
	if (sockOprResult != 0) {
		printf("getaddrinfo failed with error: %d\n", sockOprResult);
		WSACleanup();
		return 1;
	}

	

	// Create a SOCKET for the server to listen for client connections.
	ListenSocket = socket(addrInfo_ListenSocket->ai_family, addrInfo_ListenSocket->ai_socktype, addrInfo_ListenSocket->ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
		printf("socket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(addrInfo_ListenSocket);
		WSACleanup();
		return 1;
	}
	eventQueueIOPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0); //建立IOCP PORT


	//Create worker threads for queued tasks from IOCP
	std::vector<std::thread> thds;
	for (int i = 0; i < ThreadNo; i++)
		thds.push_back(std::thread(workerThread_RecvAndSend));

	// Setup the TCP listening socket
	sockOprResult = bind(ListenSocket, addrInfo_ListenSocket->ai_addr, (int)addrInfo_ListenSocket->ai_addrlen); 
	if (sockOprResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(addrInfo_ListenSocket);
		closesocket(ListenSocket);
		WSACleanup();
		system("pause");
		return 1;
	}

	freeaddrinfo(addrInfo_ListenSocket);

	sockOprResult = listen(ListenSocket, SOMAXCONN);
	if (sockOprResult == SOCKET_ERROR) {
		printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	while (true)
	{
		// Accept a client socket
		ClientSocket = accept(ListenSocket, NULL, NULL);
		if (ClientSocket == INVALID_SOCKET) {
			printf("accept failed with error: %d\n", WSAGetLastError());
			closesocket(ListenSocket);
			WSACleanup();
			return 1;
		}
		SockInformation* sockInfo = new SockInformation(ClientSocket);
		if (CreateIoCompletionPort((HANDLE)ClientSocket, eventQueueIOPort, (ULONG_PTR)sockInfo, 0) == NULL) //將要傳送的資料送入指定的IOCP PORT中
			printf("IOCP listen error\n");
		DWORD flags = 0, recvBytes;
		WSARecv(ClientSocket, &(sockInfo->wsaBuf), 1, &recvBytes, &flags, &(sockInfo->overlapped), NULL); //丟出去後不阻塞(unblock),會繼續往下跑,若真的有資料進來時,訊號會觸發阻塞狀態的GetQueuedCompletionStatus
	}
	system("pause");


	// cleanup
	closesocket(ClientSocket);
	WSACleanup();
	for (auto& thd : thds)
		thd.join();
	return 0;
}