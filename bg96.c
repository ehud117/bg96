#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>

#include "bg96.h"

struct atCommandFlow *currentCommand = NULL;
extern struct atCommandFlow completePostFlow[];

static int comPoartFd;
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
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(comPoartFd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

void closeComPort(void) {
	/* printf("Total time %d (%d)", totalWaits, totalWaits /1000); */
	printf("\nclosing com port\n");
	close(comPoartFd);
	printf("closed\n");
}

int64_t timeDiffMillisecond(struct timeval *x , struct timeval *y) {
	int64_t x_ms , y_ms , diff;
	
	x_ms = ((int64_t)x->tv_sec * 1000) + (x->tv_usec / 1000);
	y_ms = ((int64_t)y->tv_sec * 1000) + (y->tv_usec / 1000);
	
	diff = x_ms - y_ms;
	
	return diff;
}

int searchForMatch(char *phrase) {
	onAtResponse *possibleResponses = currentCommand->possibleResponses;
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
};

#define MAX_EXPECTED_PHRASE_LEN 25
#define MAX_EXPECTED_RESULTS  10
int totalWaits = 0;
static char possiblyExpectedPhrase[MAX_EXPECTED_PHRASE_LEN];
struct timeval tlastread, tnow;
static int writeIndex = 0;
void resetSearchInSerialPort() {
	writeIndex = 0;
	gettimeofday(&tlastread , NULL);
}

static bool validateSearchForInResponseLength() {
	onAtResponse *possibleResponses = currentCommand->possibleResponses;
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
static int findTimeoutIndex() {
	onAtResponse *possibleResponses = currentCommand->possibleResponses;
	int i;
	for (i = 0; possibleResponses[i].responseText; i++)
		;
	return i;
}
static int searchInSerialPort() {
	onAtResponse *possibleResponses = currentCommand->possibleResponses;
	
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
			case EXPECTING_LF:
				if (c == '\n') {
					state = INSIDE_A_WORD;
					writeIndex = 0;
				}
				break;
			
			case INSIDE_A_WORD:
				if (c == '\r') {
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
			
		}
	}
	return -1;
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
		printf("%c", c);
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

struct atCommandFlow *findFuncInFlow(void (*f)(void)) {
	struct atCommandFlow *command;
	for (command = completePostFlow; command->func != NULL; command++) {
		if (f == command->func)
			return command;
	}
	return NULL;
}

#define MINIMAL_TIMEOUT 300 // milliseconds
void checkModem(void) {
	if not(currentCommand)
		return;
	
	int nextCommandIndex = searchInSerialPort();
	if (nextCommandIndex == -1) {
		gettimeofday(&tnow , NULL);
		int millisecondsTO = 1000 * currentCommand->secondsTimeout + MINIMAL_TIMEOUT;
		if (timeDiffMillisecond(&tnow, &tlastread) > millisecondsTO) {
			printf("timeDiff %d is gt %d\n", (int)timeDiffMillisecond(&tnow, &tlastread), millisecondsTO);
			nextCommandIndex = findTimeoutIndex();	
		} else {
			return;
		}
	}
	printf("%c", '\n');
	// continue to next command
	void (*nextCommandFunc)(void) = currentCommand->possibleResponses[nextCommandIndex].doOnResponseText;
	if (nextCommandFunc == NULL)
		currentCommand = NULL;
	else
		startCommand(nextCommandFunc);
}

void startCommand(void (*func)(void)) {
	currentCommand = findFuncInFlow(func);
	func();
	resetSearchInSerialPort();
}


