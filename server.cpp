#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
using namespace std;
#pragma comment(lib, "Ws2_32.lib")
#include <winsock2.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <time.h>

struct SocketState
{
	SOCKET id;			// Socket handle
	int	recv;			// Receiving?
	int	send;			// Sending?
	char buffer[4096];
	int len;
	int bytesSentSoFar;
	int bytesToSend;
	time_t lastActiveTime;
};

const int TIME_PORT = 8080;	// Tried also 27015, didnt work
const int MAX_SOCKETS = 10;
const int EMPTY = 0;
const int LISTEN = 1;
const int RECEIVE = 2;
const int IDLE = 3;
const int SEND = 4;

bool addSocket(SOCKET id, int what, SocketState sockets[], int& socketsCount);
void removeSocket(int index, SocketState sockets[], int& socketsCount);
void acceptConnection(int index, SocketState sockets[], int& socketsCount);
void receiveMessage(int index, SocketState sockets[], int& socketsCount);
void handleClientCommand(int index, SocketState sockets[], int& socketsCount);

void handleHttpOptions(int index, SocketState sockets[]);

void handleHttpGet(int index, const string& path, SocketState sockets[]);
string resolveFilename(const string& path);
string buildGetResponse(const string& filename);
void sendHttpResponse(int index, const string& response, SocketState sockets[]);
void sendMessage(int index, SocketState sockets[], int& socketsCount);
void handleHttpHead(int index, const string& path, SocketState sockets[]);
string buildHeadResponse(const string& filename);

void handleHttpPost(int index, const string& path, const string& requestLine, SocketState sockets[]);

void handleHttpPut(int index, const string& path, const string& requestLine, SocketState sockets[]);

void handleHttpDelete(int index, const string& path, SocketState sockets[]);

void handleHttpTrace(int index, const string& fullRequest, SocketState sockets[]);


//------------------------------------------------------ MAIN ------------------------------------------------------------------

//            socket()
// prepering the address of the server
//            bind()
//           listen()
//           select()
// accept() / recv() / send()

void main()
{
	struct SocketState sockets[MAX_SOCKETS] = { 0 };
	struct timeval timeout;
	timeout.tv_sec = 5; 
	timeout.tv_usec = 0;

	int socketsCount = 0;

	WSAData wsaData;
	
	for (int i = 0; i < MAX_SOCKETS; i++) {
		sockets[i].id = INVALID_SOCKET;
		sockets[i].recv = EMPTY;
		sockets[i].send = EMPTY;
	}

	// Initialize Winsock (version 2.2) and check for errors
	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		cout << "Time Server: Error at WSAStartup()\n";
		return;
	}

	// Create a TCP socket (AF_INET, SOCK_STREAM, IPPROTO_TCP) for incoming connections
	SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Check if socket creation failed and print error if needed
	if (INVALID_SOCKET == listenSocket)
	{
		cout << "Time Server: Error at socket(): " << WSAGetLastError() << endl;
		WSACleanup();
		return;
	}
	
	sockaddr_in serverService;					// Prepare sockaddr_in with IP and port for binding
	serverService.sin_family = AF_INET;			// Address family (must be AF_INET - Internet address family).
	serverService.sin_addr.s_addr = INADDR_ANY;	// Set IP address to INADDR_ANY to accept connections on all interfaces
	serverService.sin_port = htons(TIME_PORT);	// Set port using htons to convert to network byte order


	// Bind the socket to the specified IP and port
	if (SOCKET_ERROR == bind(listenSocket, (SOCKADDR*)&serverService, sizeof(serverService)))
	{
		cout << "Time Server: Error at bind(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}

	// Start listening for incoming connections (backlog set to 5)
	if (SOCKET_ERROR == listen(listenSocket, 5))
	{
		cout << "Time Server: Error at listen(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}

	// Add the listening socket to the sockets array to monitor for new incoming connection requests
	addSocket(listenSocket, LISTEN, sockets, socketsCount);
	cout << "HTTP Server is running on port " << TIME_PORT << " and waiting for requests..." << endl;

	// Accept connections and handles them one by one.
	while (true)
	{	
		// Use select() to check which sockets are ready for read/write operations
		// Prepare fd_set for sockets waiting to receive data (rest them)
		fd_set waitRecv;
		FD_ZERO(&waitRecv);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if ((sockets[i].recv == LISTEN) || (sockets[i].recv == RECEIVE))
				FD_SET(sockets[i].id, &waitRecv);
		}

		// Prepare fd_set for sockets waiting to send data
		fd_set waitSend;
		FD_ZERO(&waitSend);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if (sockets[i].send == SEND)
				FD_SET(sockets[i].id, &waitSend);
		}

		// Wait for socket events (blocking select with no timeout)
		int nfd = select(0, &waitRecv, &waitSend, NULL, &timeout);


		// Close all the unactive sockets
		time_t currentTime = time(NULL);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if (sockets[i].recv != EMPTY && sockets[i].recv != LISTEN)
			{
				double secondsIdle = difftime(currentTime, sockets[i].lastActiveTime);
				if (secondsIdle > 120) // Bigger than 120 sec (2 minutes)
				{
					cout << "Closing idle connection after timeout (socket " << i << ")" << endl;
					closesocket(sockets[i].id);
					removeSocket(i, sockets, socketsCount);
				}
			}
		}

		for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++)
		{
			if (FD_ISSET(sockets[i].id, &waitRecv))
			{
				nfd--;
				switch (sockets[i].recv)
				{
				case LISTEN:
					acceptConnection(i, sockets, socketsCount);
					break;

				case RECEIVE:
					receiveMessage(i, sockets, socketsCount);
					break;
				}
			}
		}

		for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++)
		{
			if (FD_ISSET(sockets[i].id, &waitSend))
			{
				nfd--;
				if (sockets[i].send == SEND)
				{
					sendMessage(i, sockets, socketsCount);
				}
			}
		}
	}

	// Closing connections and Winsock.
	cout << "Time Server: Closing Connection.\n";
	closesocket(listenSocket);
	WSACleanup();
}


//--------------------------------------------------------- FUNCTIONS ---------------------------------------------------------------------
bool addSocket(SOCKET id, int what, SocketState sockets[], int& socketsCount)
{
	for (int i = 0; i < MAX_SOCKETS; i++)
	{
		if (sockets[i].recv == EMPTY)
		{
			sockets[i].id = id;
			sockets[i].recv = what;
			sockets[i].send = IDLE;
			sockets[i].len = 0;
			sockets[i].lastActiveTime = time(NULL);
			socketsCount++;
			cout << "[+] Socket added. ID: " << id << ", Total sockets: " << socketsCount << endl;
			return (true);
		}
	}
	return (false);
}

void removeSocket(int index, SocketState sockets[], int& socketsCount)
{
	closesocket(sockets[index].id);
	sockets[index].id = INVALID_SOCKET;
	sockets[index].recv = EMPTY;
	sockets[index].send = EMPTY;
	socketsCount--;
}

// Accept a new incoming client connection and add its socket to the socket list
void acceptConnection(int index, SocketState sockets[], int& socketsCount)
{
	SOCKET id = sockets[index].id;
	struct sockaddr_in from;		// Address of sending partner
	int fromLen = sizeof(from);

	SOCKET msgSocket = accept(id, (struct sockaddr*)&from, &fromLen);
	if (INVALID_SOCKET == msgSocket)
	{
		cout << "Time Server: Error at accept(): " << WSAGetLastError() << endl;
		return;
	}
	
	// Set the socket to be in non-blocking mode.
	unsigned long flag = 1;
	if (ioctlsocket(msgSocket, FIONBIO, &flag) != 0)
	{
		cout << "Time Server: Error at ioctlsocket(): " << WSAGetLastError() << endl;
	}

	if (addSocket(msgSocket, RECEIVE, sockets, socketsCount) == false)
	{
		cout << "\t\tToo many connections, dropped!\n";
		closesocket(msgSocket);
	}
	return;
}

void receiveMessage(int index, SocketState sockets[], int& socketsCount)
{
	SOCKET msgSocket = sockets[index].id;

	int len = sockets[index].len;
	int bytesRecv = recv(msgSocket, &sockets[index].buffer[len], sizeof(sockets[index].buffer) - len, 0);


	if (SOCKET_ERROR == bytesRecv)
	{
		cout << "Time Server: Error at recv(): " << WSAGetLastError() << endl;
		closesocket(msgSocket);
		removeSocket(index, sockets, socketsCount);
		return;
	}
	if (bytesRecv == 0)
	{
		closesocket(msgSocket);
		removeSocket(index, sockets, socketsCount);
		return;
	}
	else
	{
		sockets[index].buffer[len + bytesRecv] = '\0'; //add the null-terminating to make it a string
		cout << "Time Server: Recieved: " << bytesRecv << " bytes of \"" << &sockets[index].buffer[len] << "\" message.\n";

		sockets[index].lastActiveTime = time(NULL);
		sockets[index].len += bytesRecv;

		if (sockets[index].len > 0)
		{
			handleClientCommand(index, sockets, socketsCount);
		}
	}
}


void handleClientCommand(int index, SocketState sockets[], int& socketsCount)
{
	SOCKET msgSocket = sockets[index].id;

	string requestLine = sockets[index].buffer;
	size_t startPathPos = requestLine.find(" ") + 1;
	size_t endPathPos = requestLine.find(" ", startPathPos);
	string path = requestLine.substr(startPathPos, endPathPos - startPathPos);

	cout << "[#] Handling request from socket ID " << sockets[index].id << ":\n" << sockets[index].buffer << endl;

	if (strncmp(sockets[index].buffer, "OPTIONS ", 8) == 0)
	{
		handleHttpOptions(index, sockets);
	}
	else if (strncmp(sockets[index].buffer, "GET ", 4) == 0)
	{
		handleHttpGet(index, path, sockets);
	}
	else if (strncmp(sockets[index].buffer, "POST ", 5) == 0)
	{
		handleHttpPost(index, path, requestLine, sockets);
	}
	else if (strncmp(sockets[index].buffer, "HEAD ", 5) == 0)
	{
		handleHttpHead(index, path, sockets);
	}
	else if (strncmp(sockets[index].buffer, "PUT ", 4) == 0)
	{
		handleHttpPut(index, path, requestLine, sockets);
	}
	else if (strncmp(sockets[index].buffer, "DELETE ", 7) == 0)
	{
		handleHttpDelete(index, path, sockets);
	}
	else if (strncmp(sockets[index].buffer, "TRACE ", 6) == 0)
	{
		handleHttpTrace(index, sockets[index].buffer, sockets);
	}
}

void handleHttpOptions(int index, SocketState sockets[])
{
	ostringstream response;
	response << "HTTP/1.1 200 OK\r\n"
		<< "Allow: GET, HEAD, POST, PUT, DELETE, TRACE\r\n"
		<< "Content-Length: 0\r\n"
		<< "Connection: close\r\n\r\n";

	sendHttpResponse(index, response.str(), sockets);
}

string resolveFilename(const std::string& path)
{
	string filename, lang;
	size_t questionMarkPos = path.find('?');

	if (questionMarkPos != string::npos)	// If we found '?' in the path
	{
		filename = path.substr(1, questionMarkPos - 1);			// "/index.html?lang=he"  =>   "index.html"
		string query = path.substr(questionMarkPos + 1);	// "/index.html?lang=he   =>   "lang=he"

		size_t equalMarkPos = query.find('=');
		if (equalMarkPos != string::npos && query.substr(0, equalMarkPos) == "lang")
		{
			lang = query.substr(equalMarkPos + 1);
			if (lang == "he" || lang == "en" || lang == "fr")
			{
				size_t dotMarkPos = filename.find_last_of(".");
				if (dotMarkPos != string::npos)
				{
					std::string extension = filename.substr(dotMarkPos);
					filename = filename.substr(0, dotMarkPos) + "_" + lang + extension;
				}
			}
		}
	}
	else
	{
		filename = path.substr(1);	// Get all the path as a name not including the first char
	}

	string fullPathName = "C:\\temp\\" + filename;
	//cout << "[*] Resolved filename: " << fullPathName << endl;

	return fullPathName;
}


void sendHttpResponse(int index, const string& response, SocketState sockets[])
{
	int bufferLength = sizeof(sockets[index].buffer) - 1;
	strncpy(sockets[index].buffer, response.c_str(), bufferLength);
	sockets[index].buffer[bufferLength] = '\0';

	sockets[index].len = (int)strlen(sockets[index].buffer);
	sockets[index].bytesToSend = (int)strlen(sockets[index].buffer);
	sockets[index].bytesSentSoFar = 0;

	sockets[index].send = SEND;
}

void sendMessage(int index, SocketState sockets[], int& socketsCount)
{
	SOCKET msgSocket = sockets[index].id;

	int remaining = sockets[index].bytesToSend - sockets[index].bytesSentSoFar;
	int bytesSent = send(msgSocket,	sockets[index].buffer + sockets[index].bytesSentSoFar, remaining, 0);

	if (bytesSent == SOCKET_ERROR)
	{
		cout << "Time Server: Error at send(): " << WSAGetLastError() << endl;
		closesocket(msgSocket);
		removeSocket(index, sockets, socketsCount);
		return;
	}

	sockets[index].bytesSentSoFar += bytesSent;

	if (sockets[index].bytesSentSoFar >= sockets[index].bytesToSend)
	{
		sockets[index].send = IDLE;
	}
}

// Parses the path and query string of an HTTP GET request to determine the correct filename to serve (with language suffix if needed)
void handleHttpGet(int index, const string& path, SocketState sockets[])
{
	string filename = resolveFilename(path);
	string response = buildGetResponse(filename);
	sendHttpResponse(index, response, sockets);
}

string buildGetResponse(const string& filename)
{
	ifstream file(filename);
	ostringstream response;

	// File wasn't found => send back 404
	if (!file.is_open())
	{
		string notFound = "<h1>404 Not Found</h1>";
		response << "HTTP/1.1 404 Not Found\r\n"
			     << "Content-Type: text/html\r\n"
			     << "Content-Length: " << notFound.length() << "\r\n"
			     << "Connection: close\r\n\r\n"
			     << notFound;
	}
	else
	{
		ostringstream bodyBuffer;
		bodyBuffer << file.rdbuf();			// Return pointer to the inner buffer of the input
		string body = bodyBuffer.str();		// Get the value of the file as a string

		response << "HTTP/1.1 200 OK\r\n"
			     << "Content-Type: text/html\r\n"
			     << "Content-Length: " << body.length() << "\r\n"
			     << "Connection: close\r\n\r\n"
			     << body;
	}

	return response.str();
}


void handleHttpHead(int index, const string& path, SocketState sockets[])
{
	string filename = resolveFilename(path);
	string response = buildHeadResponse(filename);
	sendHttpResponse(index, response, sockets);
}

string buildHeadResponse(const string& filename)
{
	ifstream file(filename);
	ostringstream response;

	if (!file.is_open())
	{
		response << "HTTP/1.1 404 Not Found\r\n"
			<< "Content-Type: text/html\r\n"
			<< "Content-Length: 0\r\n"
			<< "Connection: close\r\n\r\n";
	}
	else
	{
		file.seekg(0, ios::end);				// Put the curser in the end of the file
		int fileSize = (int)file.tellg();		// Find the size of the file by the location of the curser

		response << "HTTP/1.1 200 OK\r\n"
			<< "Content-Type: text/html\r\n"
			<< "Content-Length: " << fileSize << "\r\n"
			<< "Connection: close\r\n\r\n";
	}

	return response.str();
}


void handleHttpPost(int index, const string& path, const string& fullRequest, SocketState sockets[])
{
	size_t bodyStartPos = fullRequest.find("\r\n\r\n");
	string body = "";
	ostringstream response;

	if (bodyStartPos != string::npos)
	{
		body = fullRequest.substr(bodyStartPos + 4);			// We add 4 to skip the 4 chars: \r -> \n -> \r -> \n
	}

	if (body.empty()) 
	{
		cout << "POST received with empty body." << endl;
	}
	else
	{
		cout << "POST received: " << body << endl; 
	}

	response << "HTTP/1.1 200 OK\r\n"
		<< "Content-Length: 0\r\n"
		<< "Connection: close\r\n\r\n";

	sendHttpResponse(index, response.str(), sockets);
}


void handleHttpPut(int index, const string& path, const string& fullRequest, SocketState sockets[])
{
	size_t bodyStartPos = fullRequest.find("\r\n\r\n");
	string body = "";
	ostringstream response;

	if (bodyStartPos != string::npos)
	{
		body = fullRequest.substr(bodyStartPos + 4);	// We add 4 to skip the 4 chars: \r -> \n -> \r -> \n
	}

	string filename = resolveFilename(path);
	ofstream outFile(filename);

	if (!outFile.is_open())
	{
		response << "HTTP/1.1 500 Internal Server Error\r\n"
			<< "Content-Length: 0\r\n"
			<< "Connection: close\r\n\r\n";
	}
	else
	{
		outFile << body;
		outFile.close();

		response << "HTTP/1.1 200 OK\r\n"
			<< "Content-Length: 0\r\n"
			<< "Connection: close\r\n\r\n";
	}

	sendHttpResponse(index, response.str(), sockets);
}


void handleHttpDelete(int index, const string& path, SocketState sockets[])
{
	string fileName = resolveFilename(path);
	ostringstream response;

	if (remove(fileName.c_str()) == 0)
	{
		response << "HTTP/1.1 200 OK\r\n"
			<< "Content-Length: 0\r\n"
			<< "Connection: close\r\n\r\n";
	}
	else
	{
		response << "HTTP/1.1 404 Not Found\r\n"
			<< "Content-Length: 0\r\n"
			<< "Connection: close\r\n\r\n";
	}

	sendHttpResponse(index, response.str(), sockets);
}

void handleHttpTrace(int index, const string& fullRequest, SocketState sockets[])
{
	size_t bodyLength = fullRequest.length();
	ostringstream response;

	response << "HTTP/1.1 200 OK\r\n"
		<< "Content-Type: message/http\r\n"
		<< "Content-Length: " << bodyLength << "\r\n"
		<< "Connection: close\r\n\r\n"
		<< fullRequest;

	sendHttpResponse(index, response.str(), sockets);
}
