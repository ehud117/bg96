#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
/* #include <strsafe.h> */
#include <time.h>
#include <sys/time.h>
#include "bg96.h"


#define DEFAULT_MAX_RESPONSE_TIME 300

static void deactiveContextProfile(void);
static void readResponseBody(void);

#define URL "https://postman-echo.com/post"
#define POST_REQ_BODY "abcdefg"

#define MAX_AT_SYNC_TRIES 10
static int atSyncCounter;
static void atSyncContinue(void);
static void atSyncStart(void) { atSyncCounter = 0; atSyncContinue(); }
static void atSyncContinue(void) { 
	
	atSyncCounter++;
	if (atSyncCounter > MAX_AT_SYNC_TRIES) {
		startCommand(closeComPort);
		return;
	}
	
	sendCommand("AT"); 
}
static void echoOff(void)                { sendCommand("ATE0"); }
static void checkSimPart1(void)          { sendCommand("AT+CPIN?"); }
static void checkSimPart2()              { }
static void setApn(void)                 { sendCommand("AT+QICSGP=1,1,\"UNINET\",\"\",\"\",0"); }
static void activateContextProfile(void) { sendCommand("AT+QIACT=1"); }
static void configHttpContextId(void)    { sendCommand("AT+QHTTPCFG=\"CONTEXTID\",1"); }
static void configHttpSslContextId(void) { sendCommand("AT+QHTTPCFG=\"sslctxid\",1"); }
static void configSslVersion(void)       { sendCommand("AT+QSSLCFG=\"sslversion\",1,3"); }
static void configCipherSuite(void)      { sendCommand("AT+QSSLCFG=\"ciphersuite\",1,0xC027,0xC028,0xC02F,0x003D"); } /* TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256, TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384, TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, TLS_RSA_WITH_AES_256_CBC_SHA256 */
static void setUrlPart1(void)            { char temp[30]; sprintf(temp, "at+qhttpurl=%d", (int)strlen(URL)); sendCommand(temp); }
static void setUrlPart2(void)            { writeComPort(URL); }
static void setPostHeaders(void)       { char temp[30]; sprintf(temp, "at+qhttppost=%d", (int)strlen(POST_REQ_BODY)); sendCommand(temp); }
static void setPostBody(void)       { writeComPort(POST_REQ_BODY); }
static void readResponseStatus(void) {
	int err = readIntFromSerial();
	int httpResponseStatus = readIntFromSerial();
	printf("returned with err,status: %d,%d\n", err, httpResponseStatus);
			
	if (err != 0 || httpResponseStatus != 200) {
		printf("error in response detected\n");
		startCommand(deactiveContextProfile);
	} else {
		startCommand(readResponseBody);
	}
}
static void readResponseBody(void) { sendCommand("AT+QHTTPREAD");}
static void readResponseBodyErrCode() {
	int err = readIntFromSerial();
	printf("err code: %d\n", err);
	startCommand(deactiveContextProfile);
}
static void deactiveContextProfile(void) { sendCommand("AT+QIDEACT=1"); }

// Timeout according to quictek manuals (seconds)

#define CPIN_TIMEOUT          5
#define QIACT_TIMEOUT         150
#define POST_HEADER_TIMEOUT   125
#define POST_BODY_TIMEOUT     60
#define RESPONSE_READ_TIMEOUT 60
#define QIDEACT_TIMEOUT       40

struct atCommandFlow completePostFlow[] = {
	{atSyncStart, 0, (onAtResponse[])
		{{"OK", echoOff},                      {NULL, atSyncContinue}}},
	{atSyncContinue, 0, 
		(onAtResponse[]){{"OK", echoOff},      {NULL, atSyncContinue}}},
	{echoOff, 0, (onAtResponse[])
		{{"OK", checkSimPart1},                {NULL, closeComPort}}},
	{checkSimPart1, CPIN_TIMEOUT, (onAtResponse[])
		{{"+CPIN: READY", checkSimPart2},      {NULL, closeComPort}}},
	{checkSimPart2, 0, (onAtResponse[])
		{{"OK", setApn},                       {NULL, closeComPort}}},
	{setApn, 0, (onAtResponse[])
		{{"OK", activateContextProfile},       {"ERROR", closeComPort},      {NULL, closeComPort}}},
	{activateContextProfile, QIACT_TIMEOUT, (onAtResponse[])
		{{"OK", configHttpContextId},          {"ERROR", deactiveContextProfile},      { NULL, deactiveContextProfile}}},
	{configHttpContextId, 0, (onAtResponse[])
		{{"OK", configHttpSslContextId},       {"+CME ERROR", deactiveContextProfile}, {NULL, deactiveContextProfile}}},
	{configHttpSslContextId, 0, (onAtResponse[])
		{{"OK", configSslVersion},             {"+CME ERROR", deactiveContextProfile}, {NULL, deactiveContextProfile}}},
	{configSslVersion, 0, (onAtResponse[])
		{{"OK", configCipherSuite},            {"ERROR",      deactiveContextProfile},      {NULL, deactiveContextProfile}}},
	{configCipherSuite, 0, (onAtResponse[])
		{{"OK", setUrlPart1},                  {"ERROR",      deactiveContextProfile},      {NULL, deactiveContextProfile}}},
	{setUrlPart1, 0, (onAtResponse[])
		{{"CONNECT", setUrlPart2},             {"+CME ERROR", deactiveContextProfile}, {NULL, deactiveContextProfile}}},
	{setUrlPart2, 0, (onAtResponse[])
		{{"OK", setPostHeaders},             {"+CME ERROR", deactiveContextProfile}, {NULL, deactiveContextProfile}}},
	{setPostHeaders, POST_HEADER_TIMEOUT, (onAtResponse[])
		{{"CONNECT", setPostBody},        {"+CME ERROR", deactiveContextProfile}, {NULL, deactiveContextProfile}}},
	{setPostBody, POST_BODY_TIMEOUT, (onAtResponse[])
		{{"+QHTTPPOST: ", readResponseStatus}, {"+CME ERROR", deactiveContextProfile}, {NULL, deactiveContextProfile}}},
	/* {readResponseStatus,     (onAtResponse[]){{NULL, NULL}}}, // Flow is defined inside the function */
	{readResponseBody, RESPONSE_READ_TIMEOUT, (onAtResponse[])
		{{"+QHTTPREAD: ", readResponseBodyErrCode}, {"+CME ERROR: ", readResponseBodyErrCode}, {NULL, deactiveContextProfile}}},
	{deactiveContextProfile, QIDEACT_TIMEOUT, (onAtResponse[])
		{{"OK", closeComPort},                 {"ERROR", closeComPort},      {NULL, closeComPort}}},
	{NULL, 0, NULL}
};



/* int rssi; */
/* void checkSignalQuality(void) { */
/* 	char *searchFor[] = {"+CSQ: ", NULL}; */
/* 	sendCommand("AT+CSQ"); */
/* 	searchInSerialPort(1, searchFor); */
/* 	rssi = readIntFromSerial(); */
/* 	drainSerialPortReadSimple(0); */
/* 	 */
/* } */
/* static void atSync(void) { */
/* 	 */
/* 	int atSyncTryNumber; */
/* 	for (atSyncTryNumber = 0; atSyncTryNumber < MAX_AT_SYNC_TRIES; atSyncTryNumber++) { */
/* 		sendCommand("AT"); */
/* 		if (searchInSerialPort(0, searchForOkay) == 0) */
/* 			break; */
/* 	} */
/* 	if (atSyncTryNumber < MAX_AT_SYNC_TRIES) { */
/* 		printf("At synced\n"); */
/* 	} else { */
/* 		printf("couldn't perform at sync,exiting\n"); */
/* 		#<{(| goto end; |)}># */
/* 	} */
/* } */



int main(int argc, char *argv[]) {
	/* char userInput[200]; */
	if (argc != 2 || argv[1][1] != '\0') {
		printf("Usage: %s portDigit\n", argv[0]);
	}
	openComPort(argv[1][0]);
	
	int i;
	for (i = 0; ; i++) {
		printf("%s", "#"); fflush(stdout);
		usleep(20 * 1000);
		
		checkModem();
		if (i == 15) {
			startCommand(atSyncStart);
		}
	}	

	return 0;
} 

