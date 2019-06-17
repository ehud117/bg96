#ifndef __BG96__H__
#define __BG96__H__

typedef struct {
	char *responseText;
	void (*doOnResponseText)(void);
}onAtResponse ;

struct atCommandFlow {
	void (*func)(void);
	int secondsTimeout;
	onAtResponse *possibleResponses;
};

extern int  openComPort(char portDigit);
extern void closeComPort(void);
extern void sendCommand(char *command);
extern void writeComPort(char *str);
extern int  readIntFromSerial(void);
extern void checkModem(void);
extern void startCommand(void (*func)(void));

#define not(X) (!(X))
#define TEN_P3 1000

#endif
