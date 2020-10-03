#include"Headers.h"
#include "CCirQueue.h"

#define BUFSIZE 300000

// Critical Section(windows.h 필요)
struct CriticalSectionHelper : public CRITICAL_SECTION
{
	CriticalSectionHelper() { InitializeCriticalSection(this); }
	~CriticalSectionHelper() { DeleteCriticalSection(this); }
};

// Critical Section Lock
class CriticalSectionHelperLock
{
public:
	CriticalSectionHelperLock(CRITICAL_SECTION& cs) : m_pcs(&cs)
	{
		EnterCriticalSection(m_pcs);
	}

	~CriticalSectionHelperLock()
	{
		if (m_pcs != nullptr)
			LeaveCriticalSection(m_pcs);
	}
	explicit operator bool() { return true; }

protected:
	CRITICAL_SECTION* m_pcs;
};

#define CSLOCK( cs_ )   if( CriticalSectionHelperLock lock = cs_ )

// 소켓 정보 저장을 위한 구조체
typedef struct
{
	OVERLAPPED overlapped;
	SOCKET sock;

	// 애플리케이션 버퍼
	char buf[BUFSIZE + 1];

	// 송수신 바이트 수
	int recvbytes;
	int sendbytes;

	// WSABUF 구조체
	WSABUF wsabuf;

	// 소켓 넘버 및 상태
	int sock_num;
	bool sock_state;

} SOCKETINFO;

// 소켓 정보 구조체
SOCKETINFO* sock_info;

// 보관용 패킷
// Base Packet
F_tgPacketHeader* Temp_PacketHeader;
// Connect
F_gUserConnect* Temp_UserConnect;
// Spawn
F_gUserSpawn* Temp_UserSpawn;
// PosRot
F_gUserPosRot Temp_UserPosRot;
int Socket_Index = 0;

// Critical Section
CriticalSectionHelper g_csValue;

// 동시성 시각화
//marker_series series;
//span* s_connect;
//span* s_spawn;

// 소켓 입출력 함수
DWORD WINAPI WorkerThread(LPVOID arg);
// 초기화 함수
void Packet_Initialize();
void PacketValue_Initialize();
// 종료 함수();
void Packet_Destroy();
// 오류 출력 함수
void err_quit(char* msg);
void err_display(char* addr, u_short port, char* msg);

int main(int argc, char* argv[])
{
	int retval;

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;

	// 입출력 완료 포트 생성
	HANDLE hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (hcp == NULL) return -1;

	// CPU 개수 확인(CPU 개수를 얻는다. CPU 개수에 비례하여 작업자 스레드를 생성하기 위해서다.)
	SYSTEM_INFO si;
	GetSystemInfo(&si);

	// (CPU 개수 * 2) 개의 작업자 스레드 생성 - 이때 스레드 함수 인자로 입출력 완료 포트 핸들값을 전달한다.
	HANDLE hThread;
	DWORD ThreadId;
	for (int i = 0; i < (int)si.dwNumberOfProcessors * 2; i++)
	{
		hThread = CreateThread(NULL, 0, WorkerThread, hcp, 0, &ThreadId);
		if (hThread == NULL) return -1;
		CloseHandle(hThread);
	}

	// socket()
	SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET) err_quit((char*)"socket()");

	// bind()
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(9000);
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

	retval = bind(listen_sock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR) err_quit((char*)"bind()");

	// listen()
	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR) err_quit((char*)"listen()");

	// 값 초기화
	Packet_Initialize();

	// 소켓 정보 구조체를 할당하여 초기화한다.
	sock_info = new SOCKETINFO[4];
	if (sock_info == NULL) printf("[오류] 메모리가 부족합니다!\n");


	while (true)
	{
		if (Temp_UserConnect->ConnectCount < 4)
		{
			// accept()
			SOCKADDR_IN clientaddr;
			int addrlen = sizeof(clientaddr);
			SOCKET client_sock = accept(listen_sock, (SOCKADDR*)&clientaddr, &addrlen);;
			//ClientSock[SocketCount] = accept(listen_sock, (SOCKADDR*)&clientaddr, &addrlen);;

			if (client_sock == INVALID_SOCKET)
			{
				err_display(inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), (char*)"accept()");
				continue;
			}
			else printf("[TCP 서버] 클라이언트 접속: IP 주소=%s, 포트 번호=%d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

			//if (ClientSock[SocketCount] == INVALID_SOCKET)
			//{
			//	err_display((char*)"accept()");
			//	continue;
			//}
			//else printf("[TCP 서버] 클라이언트 접속: IP 주소=%s, 포트 번호=%d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

			// 소켓 입출력 완료 포트 연결 - accept() 함수가 리턴한 소켓과 입출력 완료 포트를 연결한다.
			HANDLE hResult = CreateIoCompletionPort((HANDLE)client_sock, hcp, (DWORD)client_sock, 0);
			//HANDLE hResult = CreateIoCompletionPort((HANDLE)ClientSock[SocketCount], hcp, (DWORD)ClientSock[SocketCount], 0);
			if (hResult == NULL) return -1;

			// 전체 소켓 참조용 데이터 저장
			printf("소켓 초기화\n");
			ZeroMemory(&(sock_info[Socket_Index].overlapped), sizeof(sock_info[Socket_Index].overlapped));
			sock_info[Socket_Index].sock = client_sock;
			sock_info[Socket_Index].recvbytes = 0;
			sock_info[Socket_Index].sendbytes = 0;
			sock_info[Socket_Index].wsabuf.buf = sock_info[Socket_Index].buf;
			sock_info[Socket_Index].wsabuf.len = BUFSIZE;
			sock_info[Socket_Index].sock_num = Socket_Index;
			sock_info[Socket_Index].sock_state = false;

			// WSARecv() 함수를 호출하여 비동기 입출력을 시작한다.
			printf("비동기 입출력 시작\n");
			DWORD recvbytes;
			DWORD flags = 0;

			// WSARecv() 함수는 비동기적으로 동작하므로 실제로 보낸 데이터 수는 다음 번에 루프를 돌 때 확인 할 수 있다.
			retval = WSARecv(sock_info[Socket_Index].sock, &(sock_info[Socket_Index].wsabuf), 1, &recvbytes, &flags, &(sock_info[Socket_Index].overlapped), NULL);

			if (retval == SOCKET_ERROR)
			{
				if (WSAGetLastError() != WSA_IO_PENDING) err_display(inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), (char*)"WSARecv()");
				continue;
			}			
			else
			{
				char* print_addr = inet_ntoa(clientaddr.sin_addr);
				u_short print_port = ntohs(clientaddr.sin_port);
				UINT32 print_PktID = ((F_tgPacketHeader*)sock_info[Socket_Index++].buf)->PktID;
			
				printf("[TCP/%s:%d] StartRecvPkd : %x\n", print_addr, print_port, print_PktID);
			}			
		}
	}

	// 값 삭제()
	Packet_Destroy();
	// Critical Section 삭제	
	DeleteCriticalSection(&g_csValue);
	// 윈속 종료
	WSACleanup();
	return 0;
}

DWORD WINAPI WorkerThread(LPVOID arg)
{
	// 스레드 함수 인자로 전달된 입출력 완료 포트 핸들값을 저장해둔다.
	HANDLE hcp = (HANDLE)arg;
	int retval;

	while (true)
	{
	G_SOCK_START:
		// GetQueuedCompletionStatus() 함수를 호출하여 비동기 입출력이 완료되기를 기다린다.
		DWORD cbTransferred;
		SOCKET client_sock;
		SOCKETINFO* ptr;

		retval = GetQueuedCompletionStatus(hcp, &cbTransferred, (PULONG_PTR)&client_sock, (LPOVERLAPPED*)&ptr, INFINITE);

		// 화면 출력을 위해 클라이언트 정보를 얻어 저장해둔다.
		SOCKADDR_IN clientaddr;
		int addrlen = sizeof(clientaddr);
		getpeername(ptr->sock, (SOCKADDR*)&clientaddr, &addrlen);

		// 비동기 입출력 결과를 확인한다.
		// GetQueuedCompletionStatus() 함수에서 오류가 발생 했다면 GetLastError() 함수로 오류 코드를 얻을 수 있다.
		// 그러나 이 값은 일반적인 윈도우 API 오류 코드므로 WSAGetOverlappedResult() 함수를 호출하여 올바른 소켓 오류 코드를 생성한 후,
		// err_display() 함수로 오류 문자열을 출력한다.
		if (retval == 0 || cbTransferred == 0)
		{
			if (retval == 0)
			{
				DWORD temp1, temp2;
				WSAGetOverlappedResult(ptr->sock, &(ptr->overlapped), &temp1, FALSE, &temp2);
				err_display(inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), (char*)"WSAGetOverlappedResult()");
			}
			// 오류 처리가 끝나면 소켓을 닫고,
			closesocket(ptr->sock);
			printf("[TCP 서버] 클라이언트 종료: IP 주소=%s, 포트 번호=%d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
			// 소켓 정보를 제거한다.
			delete ptr;
			continue;
		}

		// 데이터 전송량을 갱신한다.
		// 소켓 정보 구조체를 참조하면, 받은 데이터인지 혹은 보낸 데이터인지 알 수 있다.
		F_tgPacketHeader* pHeader = (F_tgPacketHeader*)ptr->buf;
		if (ptr->recvbytes == 0)
		{
			ptr->recvbytes = cbTransferred;
			ptr->sendbytes = 0;

			// Critical Section
			CSLOCK(g_csValue)
			{
				if (pHeader != NULL)
				{
					//받은 데이터 출력
					if (pHeader->PktID == PKT_TEST_CONNECT)
					{
						//series.write_flag(_T("Here is the Connect."));
						F_gUserConnect* pHeaderTemp = (F_gUserConnect*)pHeader;

						sock_info[ptr->sock_num].sock_state = true;
						pHeaderTemp->ConnectCount = (++(Temp_UserConnect->ConnectCount));
						Temp_UserSpawn->SpawnCount = Temp_UserConnect->ConnectCount;
						printf("[TCP/%s:%d] ConnectCount : %d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), pHeaderTemp->ConnectCount);
					}
					else if (pHeader->PktID == PKT_TEST_SPAWN)
					{
						//series.write_flag(_T("Here is the Spawn."));
						F_gUserSpawn* pHeaderTemp = (F_gUserSpawn*)pHeader;

						pHeaderTemp->SpawnCount = Temp_UserSpawn->SpawnCount;
						printf("[TCP/%s:%d] SpawnCount : %d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), pHeaderTemp->SpawnCount);
					}
					//else if (pHeader->PktID == PKT_TEST_POS)
					//{
					//	//series.write_flag(_T("Here is the SetPosRot."));
					//	F_gUserPosRot* pHeaderTemp = (F_gUserPosRot*)pHeader;
					//	pHeaderTemp->m_iPlayerNumber = ;
					//	pHeaderTemp->m_UserPos;
					//	pHeaderTemp->m_UserRot;
					//
					//	printf("[TCP/%s:%d] SetPosRot\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
					//}

					pHeader = NULL;
				}
			}
		}
		else ptr->sendbytes += cbTransferred;

		// 보낸 데이터가 받은 데이터보다 적으면, 아직 보내지 못한 데이터를 보낸다. 
		if (ptr->recvbytes > ptr->sendbytes)
		{
			// 데이터 보내기
			ZeroMemory(&(ptr->overlapped), sizeof(ptr->overlapped));
			ptr->wsabuf.buf = ptr->buf + ptr->sendbytes;
			ptr->wsabuf.len = ptr->recvbytes - ptr->sendbytes;

			for (int i = 0; i < Temp_UserConnect->ConnectCount; i++)
			{			
				DWORD sendbytes;

				// WSASend() 함수는 비동기적으로 동작하므로 실제로 보낸 데이터 수는 다음 번에 루프를 돌 때 확인 할 수 있다.						
				retval = WSASend(sock_info[i].sock, &(ptr->wsabuf), 1, &sendbytes, 0, &(ptr->overlapped), NULL);

				if (retval == SOCKET_ERROR)
				{
					if (WSAGetLastError() != WSA_IO_PENDING) err_display(inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), (char*)"WSASend()");
					goto G_SOCK_START;
				}
				else
				{
					char* print_addr = inet_ntoa(clientaddr.sin_addr);
					u_short print_port = ntohs(clientaddr.sin_port);
					UINT32 print_PktID = ((F_tgPacketHeader*)ptr->buf)->PktID;

					printf("[TCP/%s:%d] SendPkd : %x / PlayerNum : %d\n", print_addr, print_port, print_PktID, i + 1);
				}
			}
		}
		else
		{			
			// 소켓 정보 중 받은 데이터 수를 초기화한 후
			ptr->recvbytes = 0;

			// 데이터를 받는다.
			ZeroMemory(&(ptr->overlapped), sizeof(ptr->overlapped));
			ptr->wsabuf.buf = ptr->buf;
			ptr->wsabuf.len = BUFSIZE;

			DWORD recvbytes;
			DWORD flags = 0;

			// WSARecv() 함수는 비동기적으로 동작하므로 실제로 보낸 데이터 수는 다음 번에 루프를 돌 때 확인 할 수 있다.
			retval = WSARecv(ptr->sock, &(ptr->wsabuf), 1, &recvbytes, &flags, &(ptr->overlapped), NULL);				

			if (retval == SOCKET_ERROR)
			{
				if (WSAGetLastError() != WSA_IO_PENDING) err_display(inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), (char*)"WSARecv()");
				continue;
			}
			else
			{
				char* print_addr = inet_ntoa(clientaddr.sin_addr);
				u_short print_port = ntohs(clientaddr.sin_port);
				UINT32 print_PktID = ((F_tgPacketHeader*)ptr->buf)->PktID;
			
				printf("[TCP/%s:%d] RecvPkd : %x\n", print_addr, print_port, print_PktID);
			}
		}
	}

	return 0;
}

// 패킷 초기화
void Packet_Initialize()
{
	// Base Packet
	Temp_PacketHeader = new F_tgPacketHeader;
	Temp_PacketHeader->PktID = NULL;
	Temp_PacketHeader->PktSize = 0;

	// Sub Packet
	// F_gUserConnect
	Temp_UserConnect = (F_gUserConnect*)Temp_PacketHeader;
	Temp_UserConnect->ConnectCount = 0;

	// F_gUserSpawn
	Temp_UserSpawn = (F_gUserSpawn*)Temp_PacketHeader;
	Temp_UserSpawn->SpawnCount = 0;

	// F_gUserPosRot
	//Temp_UserPosRot = (F_gUserPosRot*)Temp_PacketHeader;
	//Temp_UserPosRot->m_iPlayerNumber = 0;
	//Temp_UserPosRot->m_UserPos.Pos_x = 0;
	//Temp_UserPosRot->m_UserPos.Pos_y = 0;
	//Temp_UserPosRot->m_UserPos.Pos_z = 0;
	//Temp_UserPosRot->m_UserRot.Rot_Yaw = 0;
	//Temp_UserPosRot->m_UserRot.Rot_Pitch = 0;
	//Temp_UserPosRot->m_UserRot.Rot_Roll = 0;

	// flag initialize
	//s_connect = new span(series, 1, _T("flag connect"));
	//s_spawn = new span(series, 2, _T("flag spawn"));
}

// 값 초기화
void PacketValue_Initialize()
{
	// Base Packet
	Temp_PacketHeader->PktID = NULL;
	Temp_PacketHeader->PktSize = 0;

	// Sub Packet
	// F_gUserConnect
	Temp_UserConnect->ConnectCount = 0;

	// F_gUserSpawn
	Temp_UserSpawn->SpawnCount = 0;

	// F_gUserPosRot
	//Temp_UserPosRot->m_iPlayerNumber = 0;
	//Temp_UserPosRot->m_UserPos.Pos_x = 0;
	//Temp_UserPosRot->m_UserPos.Pos_y = 0;
	//Temp_UserPosRot->m_UserPos.Pos_z = 0;
	//Temp_UserPosRot->m_UserRot.Rot_Yaw = 0;
	//Temp_UserPosRot->m_UserRot.Rot_Pitch = 0;
	//Temp_UserPosRot->m_UserRot.Rot_Roll = 0;

	// flag initialize
	//s_connect = new span(series, 1, _T("flag connect"));
	//s_spawn = new span(series, 2, _T("flag spawn"));
}

// 값 지우기
void Packet_Destroy()
{
	// Sub Packet
	delete Temp_UserConnect;
	//delete Temp_UserPosRot;

	// Base Packet
	delete Temp_PacketHeader;

	// Delete flag
	//delete s_connect;
	//delete s_spawn;
}

// 소켓 함수 오류 출력 후 종료
void err_quit(char* msg)
{
	LPVOID lpMsgBuf;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);

	MessageBox(NULL, (LPCTSTR)lpMsgBuf, (LPCWSTR)msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(-1);
}

// 소켓 함수 오류 출력
void err_display(char* addr, u_short port, char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);

	printf("[TCP/%s:%d] %s : %s\n", addr, port, msg, (LPCTSTR)lpMsgBuf);
	LocalFree(lpMsgBuf);
}
