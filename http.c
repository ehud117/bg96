#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
/* #include <strsafe.h> */
#include <time.h>
#include <ctype.h>
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define not(X) (!(X))


int comPoartFd;
int openComPort(char portDigit) {
    struct termios tty;
    /* int parity = 0; */
    int speed = B115200;
    
    char portname[] = "/dev/ttyUSB?";
    *(strchr(portname, '?')) = portDigit;
    
    /* int wlen; */

    comPoartFd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    

    if (tcgetattr(comPoartFd, &tty) < 0) {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8-bit characters */
    tty.c_cflag &= ~PARENB;     /* no parity bit */
    tty.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

    /* setup for non-canonical mode */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;

    if (tcsetattr(comPoartFd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

#define DEFAULT_MAX_RESPONSE_TIME 300

int64_t timeDiffMillisecond(struct timeval *x , struct timeval *y) {
	int64_t x_ms , y_ms , diff;
	
	x_ms = ((int64_t)x->tv_sec * 1000) + (x->tv_usec / 1000);
	y_ms = ((int64_t)y->tv_sec * 1000) + (y->tv_usec / 1000);
	
	diff = x_ms - y_ms;
	
	return diff;
}

static void drainSerialPortInput() {
	uint8_t c;
	
	while (read(comPoartFd, &c, 1) == 1)
			printf("%c", c);
	printf("\ndoneDrain\n");
}

void sendCommand(char *command) {
	drainSerialPortInput();
	printf("sending command %s\n", command);
	write(comPoartFd, (uint8_t *)command, strlen(command));
	write(comPoartFd, (uint8_t *)"\r", 1);
}

void writeComPort(char *str) {
	printf("writing %s\n", str);
	write(comPoartFd, str, strlen(str));
}

int readIntFromSerial(void) {
	uint8_t c;
	int ret = 0, digit;
	struct timeval tlastread, tnow;
	
	gettimeofday(&tlastread , NULL);
	int millisecondsTO = 500;
	
	do {
		if (read(comPoartFd, &c, 1) == 0)
			continue;
		printf("-%c %d\n", c, c);
		if (isdigit(c))
			digit = c - '0';
		else
			break;
		ret *= 10;
		ret += digit;
		
		gettimeofday(&tnow , NULL);
	} while (timeDiffMillisecond(&tnow, &tlastread) < millisecondsTO);
	return ret;
}
struct onAtResponse {
	char *responseText;
	void (*doOnResponseText)(void);
};

struct atCommandFlow {
	void (*func)(void);
	struct onAtResponse *possibleResponses;
	/* int millisecondsTimeout; */
};
struct atCommandFlow *findFuncInFlow(void (*f)(void));

static struct atCommandFlow *currentCommand = NULL;

static void deactiveContextProfile(void);
#define URL "https://postman-echo.com/post"
#define POST_REQ_BODY "abcdefg"
static void atSync(void)                 { sendCommand("AT"); }
static void echoOff(void)                { sendCommand("ATE0"); }
static void checkSimPart1(void)          { sendCommand("AT+CPIN?"); }
static void checkSimPart2()              { }
static void setApn(void)                 { sendCommand("AT+QICSGP=1,1,\"UNINET\",\"\",\"\",0"); }
static void activateContextProfile(void) { sendCommand("AT+QIACT=1"); }
static void configHttpContextId(void)    { sendCommand("AT+QHTTPCFG=\"CONTEXTID\",1"); }
static void configHttpSslContextId(void) { sendCommand("AT+QHTTPCFG=\"sslctxid\",1"); }
static void configSslVersion(void)       { sendCommand("AT+QSSLCFG=\"sslversion\",1,3"); }
/* TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256, TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,
   TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, TLS_RSA_WITH_AES_256_CBC_SHA256 */
static void configCipherSuite(void)      { sendCommand("AT+QSSLCFG=\"ciphersuite\",1,0xC027,0xC028,0xC02F,0x003D"); } // TODO: correct ciphersuite 
static void setUrlPart1(void)            { char temp[30]; sprintf(temp, "at+qhttpurl=%d", (int)strlen(URL)); sendCommand(temp); }
static void setUrlPart2(void)            { writeComPort(URL); }
static void setPostBodyPart1(void)       { char temp[30]; sprintf(temp, "at+qhttppost=%d", (int)strlen(POST_REQ_BODY)); sendCommand(temp); }
static void setPostBodyPart2(void)       { writeComPort(POST_REQ_BODY); }
static void readResponseStatus(void) {
	int err = readIntFromSerial();
	int httpResponseStatus = readIntFromSerial();
	printf("returned with err,status: %d,%d\n", err, httpResponseStatus);
			
	if (err != 0 || httpResponseStatus != 200)
		printf("error in response detected\n");
	
	currentCommand = findFuncInFlow(deactiveContextProfile);
	currentCommand->func();
}
static void deactiveContextProfile(void) { sendCommand("AT+QIDEACT=1"); }

static void closeComPort(void) {
	
	/* printf("Total time %d (%d)", totalWaits, totalWaits /1000); */
	printf("\nclosing com port\n");
	close(comPoartFd);
	printf("closed\n");
}
struct atCommandFlow completePostFlow[] = {
	{atSync,                 (struct onAtResponse[]){{"OK", echoOff},                      {NULL, closeComPort}}},
	{echoOff,                (struct onAtResponse[]){{"OK", checkSimPart1},                {NULL, closeComPort}}},
	{checkSimPart1,          (struct onAtResponse[]){{"+CPIN: READY", checkSimPart2},      {NULL, closeComPort}}},
	{checkSimPart2,          (struct onAtResponse[]){{"OK", setApn},                       {NULL, closeComPort}}},
	{setApn,                 (struct onAtResponse[]){{"OK", activateContextProfile},       {"ERROR", closeComPort},      {NULL, closeComPort}}},
	{activateContextProfile, (struct onAtResponse[]){{"OK", configHttpContextId},          {"ERROR", closeComPort},      { NULL, closeComPort}}},
	{configHttpContextId,    (struct onAtResponse[]){{"OK", configHttpSslContextId},       {"+CME ERROR", closeComPort}, {NULL, closeComPort}}},
	{configHttpSslContextId, (struct onAtResponse[]){{"OK", configSslVersion},             {"+CME ERROR", closeComPort}, {NULL, closeComPort}}},
	{configSslVersion,       (struct onAtResponse[]){{"OK", configCipherSuite},            {"ERROR", closeComPort},      {NULL, closeComPort}}},
	{configCipherSuite,      (struct onAtResponse[]){{"OK", setUrlPart1},                  {"ERROR", closeComPort},      {NULL, closeComPort}}},
	{setUrlPart1,            (struct onAtResponse[]){{"CONNECT", setUrlPart2},             {"+CME ERROR", closeComPort}, {NULL, closeComPort}}},
	{setUrlPart2,            (struct onAtResponse[]){{"OK", setPostBodyPart1},             {"+CME ERROR", closeComPort}, {NULL, closeComPort}}},
	{setPostBodyPart1,       (struct onAtResponse[]){{"CONNECT", setPostBodyPart2},        {"+CME ERROR", closeComPort}, {NULL, closeComPort}}},
	{setPostBodyPart2,       (struct onAtResponse[]){{"+QHTTPPOST: ", readResponseStatus}, {"+CME ERROR", closeComPort}, {NULL, closeComPort}}},
	{readResponseStatus,     (struct onAtResponse[]){{NULL, NULL}}}, // Flow is defined inside the function
	{deactiveContextProfile, (struct onAtResponse[]){{"OK", closeComPort},                 {"ERROR", closeComPort},      {NULL, closeComPort}}}
};

struct atCommandFlow *findFuncInFlow(void (*f)(void)) {
	int i;
	for (i = 0; i < sizeof(completePostFlow) / sizeof(*completePostFlow); i++) {
		if (f == completePostFlow[i].func)
			return &completePostFlow[i];
	}
	return NULL;
}
#define MAX_EXPECTED_PHRASE_LEN 25
#define MAX_EXPECTED_RESULTS  10
bool validateSearchForInResponseLength() {
	struct onAtResponse *possibleResponses = currentCommand->possibleResponses;
	int i;
	for (i = 0; possibleResponses[i].responseText; i++) {
		if (strlen(possibleResponses[i].responseText) > MAX_EXPECTED_PHRASE_LEN - 1) { 
			printf("error searching string %s is too long\n", possibleResponses[i].responseText);
			return false;
		}
		if (i >= MAX_EXPECTED_RESULTS) {
			printf("Too many items to search for in result(are you missing closing NULL?)\n");
			return false;
		}
	}
	return true;
}

int searchForMatch(char *phrase) {
	struct onAtResponse *possibleResponses = currentCommand->possibleResponses;
	int i;
	for (i = 0; possibleResponses[i].responseText; i++) {
		if (strcmp(possibleResponses[i].responseText, phrase) == 0)
			return i;
	}
	return -1;
}

enum {
	EXPECTING_CR,
	EXPECTING_LF,
	INSIDE_A_WORD,
	EXPECTING_ENDING_LF
};

int totalWaits = 0;
static char possiblyExpectedPhrase[MAX_EXPECTED_PHRASE_LEN];
struct timeval tlastread, tnow;
static int writeIndex = 0;
void resetSearchInSerialPort() {
	writeIndex = 0;
}
int searchInSerialPort() {
	struct onAtResponse *possibleResponses = currentCommand->possibleResponses;
	
	if not(validateSearchForInResponseLength(possibleResponses))
		return -1;
	
	int state = EXPECTING_CR;
	uint8_t c;
	while (read(comPoartFd, &c, 1) == 1) {
		printf("%c", c);
		gettimeofday(&tlastread , NULL);
		switch (state) {
			case EXPECTING_CR:
				if (c == '\r')
					state = EXPECTING_LF;
				break;
				
			case EXPECTING_LF:
				if (c == '\n') {
					state = INSIDE_A_WORD;
					writeIndex = 0;
				}
				break;
			
			case INSIDE_A_WORD:
				if (c == '\r') {
					if (writeIndex != 0)
						state = EXPECTING_ENDING_LF;
					else
						state = EXPECTING_LF;	
				} else {
					possiblyExpectedPhrase[writeIndex++] = c;
					possiblyExpectedPhrase[writeIndex] = '\0';
					int retVal = searchForMatch(possiblyExpectedPhrase);
					if (retVal != -1) {
						printf("%c", '\n');
						return retVal;
					}
				}
				break;
			
			case EXPECTING_ENDING_LF:
				if (c== '\n')
					state = EXPECTING_CR;
				break;
				
		}
	}
	return -1;
}

void checkModem(void) {
	if not(currentCommand)
		return;
	
	int searchResult = searchInSerialPort();
	if (searchResult == -1)
		return;
	printf("%c", '\n');
	// continue to next command
	void (*nextCommandFunc)(void) = currentCommand->possibleResponses[searchResult].doOnResponseText;
	if (nextCommandFunc == NULL)
		return;
	currentCommand = findFuncInFlow(nextCommandFunc);
	nextCommandFunc();
}

char *searchForOkay[] = {"OK", NULL};
char *searchForConnect[] = {"CONNECT", NULL};
#define MAX_AT_SYNC_TRIES 10

// Timeout according to quictek manuals (seconds)
#define AT_CPIN_TIMEOUT 5
#define AT_QIACT_TIMEOUT 150
#define AT_QIDEACT_TIMEOUT 40
#define POST_HEADER_TIMEOUT   125
#define POST_BODY_TIMEOUT   60

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
		if (i == 5) {
			currentCommand = &completePostFlow[0];
			currentCommand->func();
		}
	}	

	return 0;
} 

