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

#define MAX_EXPECTED_PHRASE_LEN 25
#define MAX_EXPECTED_RESULTS  10
bool validateSearchForInResponseLength(char **searchForInResponse) {
	int i;
	for (i = 0; searchForInResponse[i]; i++) {
		if (strlen(searchForInResponse[i]) > MAX_EXPECTED_PHRASE_LEN - 1) { 
			printf("error searching string %s is too long\n", searchForInResponse[i]);
			return false;
		}
		if (i >= MAX_EXPECTED_RESULTS) {
			printf("Too many items to search for in result(are you missing closing NULL?)\n");
			return false;
		}
	}
	return true;
}

int searchForMatch(char **searchForInResponse, char *phrase) {
	int i;
	for (i = 0; searchForInResponse[i]; i++) {
		if (strcmp(searchForInResponse[i], phrase) == 0)
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
int searchInSerialPort(int secondsTO, char **searchForInResponse) {
	char possiblyExpectedPhrase[MAX_EXPECTED_PHRASE_LEN];
	
	if not(validateSearchForInResponseLength(searchForInResponse))
		return -1;
	
	int retVal = -1;
	struct timeval tlastread, tnow, tfirstread;;
	
	gettimeofday(&tlastread , NULL);
	tfirstread = tlastread;
	
	int millisecondsTO = secondsTO * 1000;
	millisecondsTO = millisecondsTO ?: DEFAULT_MAX_RESPONSE_TIME + 2;
	
	int state = EXPECTING_CR;
	int writeIndex;
	do {
		uint8_t c;
		if (read(comPoartFd, &c, 1) != 0) {
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
						retVal = searchForMatch(searchForInResponse, possiblyExpectedPhrase);
						if (retVal != -1)
							goto foundExpected;
					}
					break;
				
				case EXPECTING_ENDING_LF:
					if (c== '\n')
						state = EXPECTING_CR;
					break;
					
			}
		}
		gettimeofday(&tnow , NULL);
	} while (timeDiffMillisecond(&tnow, &tlastread) < millisecondsTO);
foundExpected:
	totalWaits += (int)timeDiffMillisecond(&tnow, &tfirstread);
	printf("\ntotal time %dms\n", (int)timeDiffMillisecond(&tnow, &tfirstread));
	if (retVal == -1)
		printf("Did not find expected result in timely manner\n");
	printf("done\n");
	return retVal;
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

char *searchForOkay[] = {"OK", NULL};
char *searchForConnect[] = {"CONNECT", NULL};
#define MAX_AT_SYNC_TRIES 10

// Timeout according to quictek manuals (seconds)
#define AT_CPIN_TIMEOUT 5
#define AT_QIACT_TIMEOUT 150
#define AT_QIDEACT_TIMEOUT 40
#define POST_HEADER_TIMEOUT   125
#define POST_BODY_TIMEOUT   60

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

/* int rssi; */
/* void checkSignalQuality(void) { */
/* 	char *searchFor[] = {"+CSQ: ", NULL}; */
/* 	sendCommand("AT+CSQ"); */
/* 	searchInSerialPort(1, searchFor); */
/* 	rssi = readIntFromSerial(); */
/* 	drainSerialPortReadSimple(0); */
/* 	 */
/* } */

int main(int argc, char *argv[]) {
	/* char userInput[200]; */
	if (argc != 2 || argv[1][1] != '\0') {
		printf("Usage: %s portDigit\n", argv[0]);
	}
	openComPort(argv[1][0]);
	
	
	int atSyncTryNumber;
	for (atSyncTryNumber = 0; atSyncTryNumber < MAX_AT_SYNC_TRIES; atSyncTryNumber++) {
		sendCommand("AT");
		if (searchInSerialPort(0, searchForOkay) == 0)
			break;
	}
	if (atSyncTryNumber < MAX_AT_SYNC_TRIES) {
		printf("At synced\n");
	} else {
		printf("couldn't perform at sync,exiting\n");
		goto end;
	}
		
	// Set echo off
	sendCommand("ATE0");
	searchInSerialPort(0, searchForOkay);
	
	sendCommand("AT+CPIN?");
	if (searchInSerialPort(AT_CPIN_TIMEOUT + 1, (char *[]){"+CPIN: READY", NULL}) != 0)
		printf("Error in Sim card\n");
	searchInSerialPort(0, searchForOkay);
	
	/* Use AT+QICSGP=1,1,"UNINET","","",0 to set APN as "UNINET",user name as "",password as ""*/
	sendCommand("AT+QICSGP=1,1,\"UNINET\",\"\",\"\",0");
	searchInSerialPort(1, (char *[]){"OK", "ERROR", NULL});

	/* checkSignalQuality(); */
	/* printf("rssi is %d\n", rssi); */
	
	/* Activate context profile */	
	sendCommand("AT+QIACT=1");
	searchInSerialPort(AT_QIACT_TIMEOUT + 1, (char *[]){"OK", "ERROR", NULL});
	
	
	sendCommand("AT+QHTTPCFG=\"CONTEXTID\",1");
	searchInSerialPort(0, (char *[]){"OK", "+CME ERROR", NULL});
	
	sendCommand("AT+QHTTPCFG=\"sslctxid\",1");
	searchInSerialPort(0, (char *[]){"OK", "+CME ERROR", NULL});
	
	sendCommand("AT+QSSLCFG=\"sslversion\",1,3");
	searchInSerialPort(0, (char *[]){"OK", "ERROR", NULL});
	
	sendCommand("AT+QSSLCFG=\"ciphersuite\",1,0xFFFF"); // TODO: correct ciphersuite
	searchInSerialPort(0, (char *[]){"OK", "ERROR", NULL});
	
	
#define URL "https://postman-echo.com/post"
	char temp[30];
	sprintf(temp, "at+qhttpurl=%d", (int)strlen(URL));
	sendCommand(temp);
	searchInSerialPort(0, (char *[]){"CONNECT", "+CME ERROR", NULL});
	
	// Write the URL.
	writeComPort(URL);
	searchInSerialPort(0, (char *[]){"OK", "+CME ERROR", NULL});
	
	
#define POST_REQ_BODY "abcdefg"
	sprintf(temp, "at+qhttppost=%d", (int)strlen(POST_REQ_BODY));
	sendCommand(temp);
	searchInSerialPort(POST_HEADER_TIMEOUT + 1, (char *[]){"CONNECT", "+CME ERROR", NULL});
	
	// Write the POST_REQ_BODY.
	writeComPort(POST_REQ_BODY);
	searchInSerialPort(POST_BODY_TIMEOUT + 1, (char *[]) {"+QHTTPPOST: ", "+CME ERROR", NULL});
	int err = readIntFromSerial();
	int httpResponseStatus = readIntFromSerial();
	printf("returned with err,status: %d,%d\n", err, httpResponseStatus);
			
	if (err != 0 || httpResponseStatus != 200)
		printf("error in response detected\n");
				
	/* Use AT+QIDEACT=1 to deactivate GPRS context */
	sendCommand("AT+QIDEACT=1");
	searchInSerialPort(AT_QIDEACT_TIMEOUT + 1, (char *[]){"OK", "ERROR", NULL});
end:
	printf("Total time %d (%d)", totalWaits, totalWaits /1000);
	printf("closing com port\n");
	close(comPoartFd);
	printf("closed\n");
	return 0;
} 

