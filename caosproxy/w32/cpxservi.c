/*
 * caosprox - CPX server reference implementation
 * Written starting in 2022 by contributors (see CREDITS.txt)
 * To the extent possible under law, the author(s) have dedicated all copyright and related and neighboring rights to this software to the public domain worldwide. This software is distributed without any warranty.
 * You should have received a copy of the CC0 Public Domain Dedication along with this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

// With extreme thanks to http://double.nz/creatures/developer/sharedmemory.htm

// Internals module - contains the actual server
#include <stdlib.h>
#include <stdio.h>
#include <winsock2.h>
#include <windows.h>

#include "libcpx.h"

static SOCKET serverSocket;
// Initialize
int cpxservi_serverInit(int host, int port) {
	// deal with WS nonsense
	libcpx_initWinsock();
	// actual stuff
	serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (serverSocket == INVALID_SOCKET) {
		puts("caosprox could not make socket");
		return 1;
	}
	// binding (here's the scary part)
	struct sockaddr_in bindTarget = {
		.sin_family = AF_INET,
		.sin_addr = {
			.s_addr = host
		},
		.sin_port = htons(port),
	};
	if (bind(serverSocket, (struct sockaddr *) &bindTarget, sizeof(bindTarget))) {
		puts("caosprox could not bind to socket");
		return 1;
	}
	// doing great
	if (listen(serverSocket, SOMAXCONN)) {
		puts("caosprox failed to listen on socket");
		return 1;
	}
	return 0;
}

const char * cpxservi_gameID = NULL;
const char * cpxservi_gamePath = NULL;

static char gameIDBuffer[8192];
static char gamePathBuffer[8192];
static char tmpBuffer[8200]; // above + 8 chars for _request

static const char * findRegKey(HKEY hive, const char * base, const char * sub, const char * def, char * buffer) {
	HKEY key;
	if (!RegOpenKeyA(hive, base, &key)) {
		LONG bufferLen = 8191;
		DWORD throwaway; // I feel like there's a story to this being required.
		int result = RegQueryValueExA(key, sub, NULL, &throwaway, buffer, &bufferLen);
		RegCloseKey(key);
		if (!result) {
			// only try this if it succeeds - ERROR_MORE_DATA will cause it to run off the end of the buffer
			buffer[bufferLen] = 0;
			return buffer;
		}
		printf("caosprox registry warning, found \"%s\" but asking for \"%s\" got %x\n", base, sub, result);
	} else {
		printf("caosprox registry warning, failed to find \"%s\":\"%s\"\n", base, sub);
	}
	return def;
}

static const char * findGameID() {
	if (cpxservi_gameID)
		return cpxservi_gameID;
	return findRegKey(HKEY_CURRENT_USER, "Software\\CyberLife Technology\\Creatures Engine", "Default Game", "Docking Station", gameIDBuffer);
}

static const char * findGamePath(const char * gameID) {
	if (cpxservi_gamePath)
		return cpxservi_gamePath;
	const char * base = "Software\\CyberLife Technology\\";
	int baseLen = strlen(base);
	char name[baseLen + strlen(gameID) + 1];
	strcpy(name, base);
	strcpy(name + baseLen, gameID);
	return findRegKey(HKEY_LOCAL_MACHINE, name, "Main Directory", "C:\\Program Files (x86)\\Docking Station\\", gameIDBuffer);
}

static void transferAreaToClient(SOCKET client, const libcpx_shmHeader_t * area, int hdrOnly) {
	libcpx_sputa(client, area, hdrOnly ? 24 : (area->sizeBytes + 24));
}

static void sendStringResponse(SOCKET client, const char * text) {
	libcpx_shmHeader_t * tmpErr = (libcpx_shmHeader_t *) tmpBuffer;
	memcpy(tmpErr->magic, "c2e@", 4);
	strcpy(tmpErr->data, text);
	tmpErr->pid = 0;
	tmpErr->resultCode = 0;
	tmpErr->sizeBytes = strlen(tmpErr->data) + 1;
	tmpErr->maxSizeBytes = 8192;
	tmpErr->padding = 0;
	transferAreaToClient(client, tmpErr, 0);
}

// Note! doubleSend is 1 if we haven't sent the initial 24-byte header.
static void internalError(SOCKET client, const char * text, int doubleSend) {
	libcpx_shmHeader_t * tmpErr = (libcpx_shmHeader_t *) tmpBuffer;
	memcpy(tmpErr->magic, "c2e@", 4);
	sprintf(tmpErr->data, "caosprox: %s", text);
	tmpErr->pid = 0;
	tmpErr->resultCode = 1;
	tmpErr->sizeBytes = strlen(tmpErr->data) + 1;
	tmpErr->maxSizeBytes = 8192;
	tmpErr->padding = 0;
	if (doubleSend)
		transferAreaToClient(client, tmpErr, 1);
	transferAreaToClient(client, tmpErr, 0);
}

static void handleClientWithEverything(SOCKET client, const char * gameID, libcpx_shmHeader_t * shm, HANDLE resultEvent, HANDLE requestEvent, HANDLE process) {
	// send SHM state to client
	transferAreaToClient(client, shm, 1);
	// now we want a size back
	int size;
	if (libcpx_sgeta(client, &size, 4) != 4) {
		internalError(client, "failed to get request size", 0);
		return;
	}
	if (size > shm->maxSizeBytes) {
		internalError(client, "request size exceeds maximum size", 0);
		return;
	}
	// can't hurt
	shm->sizeBytes = size;
	if (libcpx_sgeta(client, shm->data, size) != size) {
		internalError(client, "failed to get request body", 0);
		return;
	}
	// WAIT! This could be intended for CPX.
	if ((size >= 8) && !memcmp(shm->data, "cpx-fwd:", 8)) {
		// The client wants us to forward this along as if nothing happened.
		memmove(shm->data, shm->data + 8, size - 8);
		// fallthrough
	} else if ((size >= 8) && !memcmp(shm->data, "cpx-ver\n", 8)) {
		// The client wants us to give an identifier
		sendStringResponse(client, "CPX Server W32, version " LIBCPX_VERSION);
		return;
	} else if ((size >= 8) && !memcmp(shm->data, "cpx-gamepath\n", 13)) {
		// The client wants us to give the game path
		sendStringResponse(client, findGamePath(gameID));
		return;
	} else if ((size >= 4) && !memcmp(shm->data, "cpx-", 4)) {
		// It's intended for CPX but we don't recognize it.
		internalError(client, "Unrecognized CPX extension command.", 0);
		return;
	}
	// It's intended for the engine
	ResetEvent(resultEvent);
	PulseEvent(requestEvent);
	HANDLE waitHandles[2] = {process, resultEvent};
	DWORD result = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
	if (result == WAIT_OBJECT_0) {
		// This specific error means the process ended while the request was occuring.
		// This can be tested with the "bang" CAOS command or some other crashing operation.
		internalError(client, "game closed during request", 0);
	} else {
		// send the results back to the client
		transferAreaToClient(client, shm, 0);
	}
}

static void handleClientWithSHM(SOCKET client, const char * gameID, libcpx_shmHeader_t * shm) {
	HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, shm->pid);
	if (!process) {
		internalError(client, "failed to open process handle (game dead?)", 1);
		return;
	}
	sprintf(tmpBuffer, "%s_result", gameID);
	HANDLE resultEvent = OpenEventA(EVENT_ALL_ACCESS, FALSE, tmpBuffer);
	if (!resultEvent) {
		internalError(client, "failed to open result handle", 1);
		CloseHandle(process);
		return;
	}
	sprintf(tmpBuffer, "%s_request", gameID);
	HANDLE requestEvent = OpenEventA(EVENT_ALL_ACCESS, FALSE, tmpBuffer);
	if (!requestEvent) {
		internalError(client, "failed to open request handle", 1);
		CloseHandle(resultEvent);
		CloseHandle(process);
		return;
	}
	handleClientWithEverything(client, gameID, shm, resultEvent, requestEvent, process);
	CloseHandle(requestEvent);
	CloseHandle(resultEvent);
}

static void handleClientInsideMutex(SOCKET client, const char * gameID) {
	sprintf(tmpBuffer, "%s_mem", gameID);
	HANDLE fma = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, tmpBuffer);
	if (!fma) {
		internalError(client, "could not open memory handle (game not running/detection failed?)", 1);
		return;
	}
	libcpx_shmHeader_t * shm = (libcpx_shmHeader_t *) MapViewOfFile(fma, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (!shm) {
		internalError(client, "could not map view of shared memory", 1);
		CloseHandle(fma);
		return;
	}
	handleClientWithSHM(client, gameID, shm);
	UnmapViewOfFile(shm);
	CloseHandle(fma);
}

static void handleClient(SOCKET client) {
	const char * gameID = findGameID();
	// alrighty, time to open a connection to the game, let's start with the mutex
	sprintf(tmpBuffer, "%s_mutex", gameID);
	HANDLE mutex = OpenMutexA(MUTEX_ALL_ACCESS, TRUE, tmpBuffer);
	if (!mutex) {
		internalError(client, "could not open mutex (game not running/detection failed?)", 1);
		return;
	}
	// wait to acquire the mutex
	WaitForSingleObject(mutex, INFINITE);
	// Perform stuff protected by the mutex
	handleClientInsideMutex(client, gameID);
	// Release & close
	ReleaseMutex(mutex);
	CloseHandle(mutex);
}

// Main loop
void cpxservi_serverLoop() {
	while (1) {
		// get a client
		SOCKET client = accept(serverSocket, NULL, NULL);
		if (client == INVALID_SOCKET)
			continue;
		handleClient(client);
		// and now we're done
		closesocket(client);
	}
}

