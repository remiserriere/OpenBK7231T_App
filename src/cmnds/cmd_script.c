
#include "../logging/logging.h"
#include "../new_pins.h"
#include "../new_cfg.h"
#include "../obk_config.h"
#include "../driver/drv_public.h"
#include <ctype.h>
#include "cmd_local.h"

/*
startScript test1.bat


Example 1:


// Loop demo. Features a 'goto' script command (for use within script)
// and, obviously, a label.
// Requirements: 
// - channel 1 - output relay

again:
	echo "Step 1"
	setChannel 1 0
	echo "Step 2"
	delay_s 2
	echo "Step 3"
	setChannel 1 1
	echo "Step 4"
	delay_s 2
	goto again

Example 2:

// Loop & if demo
// Requirements: 
// - channel 1 - output relay
// - channel 11 - loop variable counter

restart:
	// Channel 11 is a counter variable and starts at 0
	setChannel 11 0
again:

	// If channel 11 value reached 10, go to done
	if $CH11>=10 then goto done
	// otherwise toggle channel 1, wait and loop
	toggleChannel 1
	addChannel 11 1
	delay_ms 250
	goto again
done:
	toggleChannel 1
	delay_s 1
	toggleChannel 1
	delay_s 1
	toggleChannel 1
	delay_s 1
	toggleChannel 1
	delay_s 1
	goto restart

Example 3:

// Thread cancelation demo and exclude self demo
// Requirements: 
// - channel 1 - output relay
// - pin 8 - button
// - pin 9 - button

// 'this' is a special keyword - it mean search for script/label in this file
// 123 and 456 are unique script thread names
addEventHandler OnClick 8 startScript this label1 123
addEventHandler OnClick 9 startScript this label2 456

label1:
	// stopScript ID bExcludeSelf
	// this will stop all other instances
	stopScript 456 1
	stopScript 123 1
	setChannel 1 0
	delay_s 1
	setChannel 1 1
	delay_s 1
	setChannel 1 0
	delay_s 1
	setChannel 1 1
	delay_s 1
	setChannel 1 0
	delay_s 1
	setChannel 1 1
	delay_s 1
	exit;
label2:
	// stopScript ID bExcludeSelf
	// this will stop all other instances
	stopScript 456 1
	stopScript 123 1
	setChannel 1 0
	delay_s 0.25
	setChannel 1 1
	delay_s 0.25
	setChannel 1 0
	delay_s 0.25
	setChannel 1 1
	delay_s 0.25
	setChannel 1 0
	delay_s 0.25
	setChannel 1 1
	delay_s 0.25
	setChannel 1 0
	delay_s 0.25
	exit;



Example 4:

// Using channel value as a variable demo
// Requirements: 
// - channel 1 - output relay
// - channel 11 - you may use it as ADC, or just use setChannel 11 100 or setChannel 11 500 in console to change delay

// set default value
setChannel 11 500
// if you don't have ADC, use this to force-display 11 as a slider on GUI
setChannelType 11 dimmer1000

looper:
	setChannel 1 0
	delay_ms $CH11
	setChannel 1 1
	delay_ms $CH11
	goto looper


Example 5:

// Math operations demo.
// Just run and check channel values.

setChannelType 10 ReadOnly
setChannelType 11 ReadOnly
setChannelType 12 ReadOnly
setChannelType 13 ReadOnly
setChannelType 14 ReadOnly
setChannelType 15 ReadOnly
setChannelType 16 ReadOnly
setChannelType 17 ReadOnly
setChannelType 18 ReadOnly
setChannelType 19 ReadOnly
setChannelType 20 ReadOnly

setChannel 10 40
setChannel 10 $CH10+10

setChannel 11 $CH10
setChannel 12 10*$CH10
setChannel 13 $CH10+10
setChannel 14 $CH10>5
setChannel 15 $CH10/5
setChannel 16 $CH12+$CH11+$CH14
setChannel 17 500*0.01
setChannel 18 0.01*500
setChannel 19 1.0==1.0
setChannel 20 1.1==1.0

// Expected results:
// Channel 10 = 50
// Channel 11 = 50
// Channel 12 = 500
// Channel 13 = 60
// Channel 14 = 1
// Channel 15 = 10
// Channel 16 = 551
// Channel 17 = 5
// Channel 18 = 5
// Channel 19 = 1
// Channel 20 = 0

// Example 6:

// brightness loop
// First makes it gradually brighter, then darker
// Channel 10 is dimmer delta
// Channel 11 is delay step in ms
// NOTE: with the current state of things, this is a MQTT killer

setChannel 10 2
setChannel 11 25
led_enableAll 1

again:

if $led_dimmer>100 then "setChannel 10 -2"
if $led_dimmer<0 then "setChannel 10 2"

delay_ms $CH11
led_dimmer $led_dimmer+$CH10
goto again

Example 7:

// Button OnHoldStart and OnHold demo
// Requires a button on pin 20
// Channel 10 is used as variable
// When button hold start happens, channel 10 is set 0
// When a button is held, the value is added to channel 10 repeatedly.
// Also, when adding, the channel 10 is used as a brightness for Tasmota Devices Group

// clear previous handlers
clearAllHandlers

// make sure that DGR is running
startDriver DGR
// generate repeat after 100 ms - yes, unit here is times 100ms
// so 1 means every 100ms
// 2 means every 200ms
setButtonHoldRepeat 1
// when Hold starts, zero the variable
addEventHandler OnHoldStart 20 SetChannel 10 0
// when Hold repeats, add a 10 value as a step
// AddChannelSyntax: channelindex delta minValue maxValue
addEventHandler OnHold 20 backlog AddChannel 10 2 0 255; DGR_SendBrightness roomLEDstrips $CH10

*/

typedef struct scriptFile_s {
	char *fname;
	char *data;

	struct scriptFile_s *next;
} scriptFile_t;

typedef struct scriptInstance_s {
	scriptFile_t *curFile;
	int uniqueID;
	const char *curLine;
	int currentDelayMS;

	struct scriptInstance_s *next;
} scriptInstance_t;

#define MAX_SCRIPT_LINE 512

char *g_scrBuffer = 0;
int svm_deltaMS;
scriptFile_t *g_scriptFiles = 0;
scriptInstance_t *g_scriptThreads = 0;
scriptInstance_t *g_activeThread = 0;

scriptInstance_t *SVM_RegisterThread() {
	scriptInstance_t *r;

	r = g_scriptThreads;

	while(r) {
		if(r->curLine == 0) {
			break;
		}
		r = r->next;
	}
	if(r == 0) {
		r = malloc(sizeof(scriptInstance_t));
		memset(r,0,sizeof(scriptInstance_t));
		r->next = g_scriptThreads;
		g_scriptThreads = r;
	}
	r->uniqueID = 0;
	r->curLine = 0;
	r->curFile = 0;
	r->currentDelayMS = 0;
	return r;
}

scriptFile_t *SVM_RegisterFile(const char *fname) {
	scriptFile_t *r;

	if (!stricmp(fname, "this")) {
		if (g_activeThread != 0)
			return g_activeThread->curFile;
		return 0;
	}

	r = g_scriptFiles;

	while(r) {
		if(!stricmp(fname,r->fname)) {
			if(r->data == 0)
				return 0;
			return r;
		}
		r = r->next;
	}
	r = malloc(sizeof(scriptFile_t));
	memset(r,0,sizeof(scriptFile_t));
	r->fname = strdup(fname);
	// cast from byte* to char*
	r->data = (char*)LFS_ReadFile(fname);
	r->next = g_scriptFiles;
	g_scriptFiles = r;
	if(r->data == 0)
		return 0;
	return r;
}
const char *SVM_SkipWS(const char *p) {
	if(p==0)
		return 0;
	// skip also whitespaces
	while(*p == ' ' || *p == '\r' || *p == '\t') {
		p++;
	}
	return p;
}
const char *SVM_SkipLine(const char *p) {
	if(p==0)
		return 0;
	while(*p) {
		if(*p == '\n') {
			p++;
			return p;
		}
		p++;
	}
	return p;
}

const char *SVM_FindLabel(const char *text, const char *label, const char *fname) {
	int labLen;

	if(label == 0)
		return text;

	labLen = strlen(label);

	while(*text) {
		text = SVM_SkipWS(text); 
		if(strncmp(text,label,labLen) == 0 && text[labLen] == ':') {
			return text;
		}
		if(text[0] == '/' && text[1] == '/') {
			text = SVM_SkipLine(text); 
			text = SVM_SkipWS(text); 
		} else {
			text = SVM_SkipLine(text); 
			text = SVM_SkipWS(text); 
		}
	}
	ADDLOG_INFO(LOG_FEATURE_CMD, "Label %s not found in %s - will go to the start of file",label,fname);
	return text;
}
void SVM_RunThread(scriptInstance_t *t) {
	int maxLoops = 10;
	int loop = 0;
	const char *start, *end;
	int len, p;

	while(1) {
		loop++;
		if(t->curLine == 0) {
			t->curLine = 0;
			t->curFile = 0;
			return;
		}
		if (loop > maxLoops) {
			return;
		}
		t->curLine = SVM_SkipWS(t->curLine); 
		if(t->curLine[0] == 0) {
			t->curLine = 0;
			t->curFile = 0;
			return;
		}
		if(t->curLine[0] == '/' && t->curLine[1] == '/') {
			t->curLine = SVM_SkipLine(t->curLine); 
			t->curLine = SVM_SkipWS(t->curLine); 
		} else {
			start = t->curLine;
			end = SVM_SkipLine(start);
			t->curLine = SVM_SkipWS(end); 

			while(end > start && (end[-1]==' '||end[-1]=='\r'||end[-1]=='\n'||end[-1]=='\t')) {
				end--;
			}
			len = (end - start);
			//ADDLOG_EXTRADEBUG(LOG_FEATURE_CMD, "Script len: %i",len);

			// skip empty lines and skip labels
			if(len > 0 && start[len-1] != ':') {
				if(len >= MAX_SCRIPT_LINE) {
					len = MAX_SCRIPT_LINE-1;
				}
				memcpy(g_scrBuffer,start,len);
				g_scrBuffer[len] = 0;

				p = start-t->curFile->data;
				///ADDLOG_EXTRADEBUG(LOG_FEATURE_CMD, "[Loop %i] Script line: %s, char index %i",loop,g_scrBuffer,p);
				CMD_ExecuteCommand(g_scrBuffer,0);

				// did we get a sleep?
				if(t->currentDelayMS > 0) {
					return;
				}	
			}
		}
	}
}

void SVM_RunThreads(int deltaMS) {
	int c_sleep, c_run;

	c_sleep = 0;
	c_run = 0;
	svm_deltaMS = deltaMS;

	if(g_scrBuffer == 0) {
		g_scrBuffer = malloc(MAX_SCRIPT_LINE);
	}

	g_activeThread = g_scriptThreads;
	while(g_activeThread) {
		if(g_activeThread->currentDelayMS > 0) {
			g_activeThread->currentDelayMS -= deltaMS;
			c_sleep++;
		} else {
			SVM_RunThread(g_activeThread);
			c_run++;
		}
		g_activeThread = g_activeThread->next;
	}

	//ADDLOG_INFO(LOG_FEATURE_CMD, "SCR sleep %i, ran %i",c_sleep,c_run);
}
void SVM_GoTo(scriptInstance_t *th, const char *fname, const char *label) {
	scriptFile_t *f;

	f = SVM_RegisterFile(fname);
	if(f == 0) {

		return;
	}
	if(f->data == 0) {

		return;
	}
	if(th == 0) {

		return;
	}
	th->curFile = f;
	th->curLine = SVM_FindLabel(f->data,label,f->fname);

	return;
}
void SVM_FreeAllFiles() {
	scriptFile_t *f; 

	f = g_scriptFiles;
	while(f) {
		scriptFile_t *n;

		n = f->next;

		free(f->data);
		free(f->fname);
		free(f);

		f = n;
	}
	g_scriptFiles = 0;
}
void SVM_StopAllScripts() {
	scriptInstance_t *t;

	t = g_scriptThreads;
	while(t) {
		t->curLine = 0;
		t->curFile = 0;
		t->uniqueID = 0;
		t->currentDelayMS = 0;

		t = t->next;
	}
}
void SVM_StopScripts(int id, int bExcludeSelf) {
	scriptInstance_t *t;

	t = g_scriptThreads;
	while(t) {
		if(t == g_activeThread && bExcludeSelf) {
			// excluded
		} else {
			if(t->uniqueID == id) {
				t->curLine = 0;
				t->curFile = 0;
				t->uniqueID = 0;
				t->currentDelayMS = 0;
			} 
		}
		t = t->next;
	}
}
void SVM_GoToLocal(scriptInstance_t *th, const char *label) {

	if(th == 0) {

		return;
	}
	th->curLine = SVM_FindLabel(th->curFile->data,label,th->curFile->fname);

	return;
}
void SVM_StartScript(const char *fname, const char *label, int uniqueID) {
	scriptFile_t *f;
	scriptInstance_t *th;

	f = SVM_RegisterFile(fname);
	if(f == 0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_StartScript: failed to get file %s",fname);

		return;
	}
	if(f->data == 0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_StartScript: failed to get file %s dataa",fname);

		return;
	}
	th = SVM_RegisterThread();
	if(th == 0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_StartScript: failed to alloc thread");

		return;
	}
	th->uniqueID = uniqueID;
	th->curFile = f;
	th->curLine = SVM_FindLabel(f->data,label,f->fname);

	if(label==0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_StartScript: started %s at the beginning",fname);
	} else {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_StartScript: started %s at label %s",fname,label);
	}

	return;
}

static int CMD_GoTo(const void *context, const char *cmd, const char *args, int cmdFlags){
	const char *fname;
	const char *label;

	if(args==0||*args==0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_GoTo: command requires argument");
		return 1;
	}
	Tokenizer_TokenizeString(args,0);
	if(Tokenizer_GetArgsCount() < 1) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_GoTo: command requires 1 argument");
		return 1;
	}
	if(g_activeThread == 0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_GoTo: this can be only used from a script");
		return 1;
	}

	if(Tokenizer_GetArgsCount() == 1) {
		label = Tokenizer_GetArg(0);

		ADDLOG_EXTRADEBUG(LOG_FEATURE_CMD, "CMD_GoTo: goto local %s\n",label);

		SVM_GoToLocal(g_activeThread,label);
	} else {

		fname = Tokenizer_GetArg(0);
		label = Tokenizer_GetArg(1);
		ADDLOG_EXTRADEBUG(LOG_FEATURE_CMD, "CMD_GoTo: goto global %s %s\n",fname,label);
		SVM_GoTo(g_activeThread,fname,label);
	}

	return 1;
}

static int CMD_StartScript(const void *context, const char *cmd, const char *args, int cmdFlags){
	const char *fname;
	const char *label;
	int uniqueID;

	if(args==0||*args==0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_StartScript: command requires argument");
		return 1;
	}
	Tokenizer_TokenizeString(args,0);
	if(Tokenizer_GetArgsCount() < 1) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_StartScript: command requires 1 argument");
		return 1;
	}

	fname = Tokenizer_GetArg(0);
	label = Tokenizer_GetArg(1);
	uniqueID = Tokenizer_GetArgInteger(2);


	SVM_StartScript(fname,label,uniqueID);


	return 1;
}
static int CMD_Delay_s(const void *context, const char *cmd, const char *args, int cmdFlags){
	float del;
	int delMS;

	if(args==0||*args==0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_Delay_s: command requires argument");
		return 1;
	}
	Tokenizer_TokenizeString(args,0);
	if(Tokenizer_GetArgsCount() < 1) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_Delay_s: command requires 1 argument");
		return 1;
	}
	if(g_activeThread == 0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_Delay_s: this can be only used from a script");
		return 1;
	}

	del = Tokenizer_GetArgFloat(0);
	delMS = del * 1000;
	ADDLOG_EXTRADEBUG(LOG_FEATURE_CMD, "CMD_Delay_s: thread will delay %i extra ms\n",delMS);
	g_activeThread->currentDelayMS += delMS;


	return 1;
}
static int CMD_Delay_ms(const void *context, const char *cmd, const char *args, int cmdFlags){
	int del;

	if(args==0||*args==0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_Delay_ms: command requires argument");
		return 1;
	}
	Tokenizer_TokenizeString(args,0);
	if(Tokenizer_GetArgsCount() < 1) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_Delay_ms: command requires 1 argument");
		return 1;
	}
	if(g_activeThread == 0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_Delay_ms: this can be only used from a script");
		return 1;
	}

	del = Tokenizer_GetArgInteger(0);

	ADDLOG_EXTRADEBUG(LOG_FEATURE_CMD, "CMD_Delay_ms: thread will delay %i\n",del);
	g_activeThread->currentDelayMS += del;


	return 1;
}
static int CMD_Return(const void *context, const char *cmd, const char *args, int cmdFlags){

	if(g_activeThread == 0) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_Return: this can be only used from a script");
		return 1;
	}

	ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_Return: thread will return\n");
	g_activeThread->curFile = 0;
	g_activeThread->curLine = 0;


	return 1;
}

static int CMD_StopScript(const void *context, const char *cmd, const char *args, int cmdFlags){
	int idToStop, excludeSelf;

	Tokenizer_TokenizeString(args,0);
	if(Tokenizer_GetArgsCount() < 1) {
		ADDLOG_INFO(LOG_FEATURE_CMD, "CMD_StopScript: command requires 1 argument (unique ID)");
		return 1;
	}

	idToStop = Tokenizer_GetArgInteger(0);
	excludeSelf = Tokenizer_GetArgInteger(1);

	SVM_StopScripts(idToStop,excludeSelf);

	return 1;
}
int CMD_GetCountActiveScriptThreads() {
	scriptInstance_t *t;
	int cnt;

	cnt = 0;
	t = g_scriptThreads;
	while (t) {
		// is this an active script thread? or just a blank entry?
		if (t->curFile) {
			cnt++;
		}
		t = t->next;
	}

	return cnt;
}
static int CMD_ListScripts(const void *context, const char *cmd, const char *args, int cmdFlags){
	scriptInstance_t *t;
	int cnt;


	cnt = 0;
	t = g_scriptThreads;
	while(t) {
		if(t->curFile) {
			ADDLOG_INFO(LOG_FEATURE_CMD, "[%i] Thread UID %i - at file %s",cnt,t->uniqueID,t->curFile->fname);
		} else {
			ADDLOG_INFO(LOG_FEATURE_CMD, "[%i] Empty thread.",cnt);
		}

		cnt++;
		t = t->next;
	}

	return 1;
}
static int CMD_StopAllScripts(const void *context, const char *cmd, const char *args, int cmdFlags){


	SVM_StopAllScripts();

	return 1;
}
static int CMD_resetSVM(const void *context, const char *cmd, const char *args, int cmdFlags){


	// stop scripts
	SVM_StopAllScripts();
	// clear files
	SVM_FreeAllFiles();

	return 1;
}
void CMD_InitScripting(){
    CMD_RegisterCommand("startScript", "", CMD_StartScript, "qqqqq0", NULL);
    CMD_RegisterCommand("stopScript", "", CMD_StopScript, "qqqqq0", NULL);
    CMD_RegisterCommand("stopAllScripts", "", CMD_StopAllScripts, "qqqqq0", NULL);
    CMD_RegisterCommand("listScripts", "", CMD_ListScripts, "qqqqq0", NULL);
    CMD_RegisterCommand("goto", "", CMD_GoTo, "qqqqq0", NULL);
    CMD_RegisterCommand("delay_s", "", CMD_Delay_s, "qqqqq0", NULL);
    CMD_RegisterCommand("delay_ms", "", CMD_Delay_ms, "qqqqq0", NULL);
    CMD_RegisterCommand("return", "", CMD_Return, "qqqqq0", NULL);
    CMD_RegisterCommand("resetSVM", "", CMD_resetSVM, "qqqqq0", NULL);

}


