#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curses.h>
#include <unistd.h>

#include "main.h"

// For testing
#define NO_MPV 0

// Color indices
#define COL_ADDINGSUB 1 // You've selected the sub start point

///////////////////////////////////////
//https://mpv.io/manual/master/#list-of-input-commands

// The application should have access to this file
#define STRMLOC "/tmp/mpvtypeset"

// Defined for the srt file format
#define SUBFORMATSTRING "%d\n%s --> %s\n%s\n\n"
#define TIMEFORMAT "%02d:%02d:%02d,%03d"

// Percent format for UI
#define PERCENTFORMAT "(%.2f%%)"

// How messages are sent to mpv, change this to use something other than socat.
#define MPV_MESSAGE_FORMAT "echo %s | socat - "STRMLOC

// Seek commands sent to mpv
#define SEEK_FORMAT "no-osd seek %f exact"
#define SEEK_ABSOLUTE_FORMAT "no-osd seek %f absolute"

#define GET_PAUSE_STATUS_COMMAND "\'{ \"command\": [\"get_property\", \"pause\"]}\'"
#define GET_PERCENT_STATUS_COMMAND "\'{ \"command\": [\"get_property\", \"percent-pos\"]}\'"
#define GET_SECONDS_COMMAND "\'{ \"command\": [\"get_property\", \"playback-time\"] }\'"

// Part 1 of the pause command sent to mpv
#define PAUSE_COMMAND_SHARED "\'{ \"command\": [\"set_property\", \"pause\", "
// Different ends depending on if you're pausing or unpausing
#define PAUSE_STRING "true] }\'"
#define UNPAUSE_STRING "false] }\'"

// Actually, I think you can have blank lines.
#define BLANKLINEREPLACEMENT ""

// This is appended to some commands to prevent their output messing with curses
#define REDIRECTOUTPUT " > /dev/null"

//
#define STARTMPVFORMAT "mpv --keep-open=yes --really-quiet --input-ipc-server "STRMLOC" --no-input-terminal "STRMLOC" %s & disown"

// output mka, in audio file, in sub file
#define MAKEMKACOMMAND "mkvmerge -o %s %s %s"

// For UI, what the dividers are made of
#define DIVIDERCHAR '-'

// File extension for project files
#define BONUSEXTENSION ".rawPos"

// In seconds
#define NORMSEEK 1 // Normal
#define MEGASEEK 3
#define MINISEEK .5
#define REACTIONTIME .4 // Amount of seconds to seek back when you press d

// Not the command, but what the user sees as their action
#define SEEK_STATUS_FORMAT "Seek %f"
#define SEEK_STATUS_ABSOLUTE_FORMAT "Seek to %f"

#define QUIT_MPV_ON_END 1
// If it asking you at the end every time is annoying
#define CREATE_MKA_ON_END 1

// How many loops the message showing your last action lasts
#define REGULARACTIONHP 5

///////////////////////////////////////

// Arg and filename
char* audioFilename=NULL;
char* rawSubInFilename=NULL;
char* srtOutFilename=NULL;
char* rawOutFilename=NULL;
FILE* backupFp=NULL;

// List
int numRawSubs;
char** rawSubs=NULL;
double* rawStartTimes=NULL;
double* rawEndTimes=NULL;

// curses
WINDOW* mainwin;
char canColors=0;

// UI
int listTopPad = 2; // title, divider
int listBottomPad = 3; // divider, last action, timestamp
int listDrawLength;
int listHalfDrawLength;
int drawCursorY;
int totalKeysBound=0;
keyFunc* boundFuncs=NULL;
int* boundKeys=NULL;

// Dynamic
char addingSub=0;
int currentSubIndex=0;
char* lastAction=NULL;
int lastActionHP=0;
double addSubTime; // Timestamp where you start adding the current subtitle
char running=1;

///////////////////////////////////////
void testMessage(char* str){
	erase();
	mvprintw(0,0,str);
	refresh();
	while(getch()==ERR);
}

#if NO_MPV
	#include <time.h>
	long _startTime=0;
	long testMS(){
		struct timespec _myTime;
		clock_gettime(CLOCK_MONOTONIC, &_myTime);
		return _myTime.tv_nsec/1000000+_myTime.tv_sec*1000;
	}
#endif

// Not for curses
// getc but it ignores newline character
int goodGetC(FILE* fp){
	int _answer;
	do{
		_answer = getc(stdin);
	}while(_answer==10);
	return _answer;
}

char fileExist(char* filename){
	FILE* fp = fopen(filename,"r");
	if (fp!=NULL){
		fclose(fp);
		return 1;
	}
	return 0;
}

FILE* sendMpvCommand(char* msg, char _getOutput){
	char buff[strlen(MPV_MESSAGE_FORMAT)+strlen(msg)+strlen(REDIRECTOUTPUT)];
	sprintf(buff,MPV_MESSAGE_FORMAT,msg);
	if (!_getOutput){
		strcat(buff,REDIRECTOUTPUT); // Apply output redirection if we don't need it.
	}
	if (_getOutput){
		return popen(buff,"r");
	}else{
		system(buff);
		return NULL;
	}
}

void togglePause(){
	char dresult[256];
	FILE* fp = sendMpvCommand(GET_PAUSE_STATUS_COMMAND,1);
	dresult[fread(dresult,sizeof(dresult)-1,1,fp)]='\0';
	fclose(fp);
	if (strstr(dresult,"success")!=NULL){
		if (strstr(dresult,"true")!=NULL){
			sendMpvCommand(PAUSE_COMMAND_SHARED UNPAUSE_STRING,0);
			setLastAction("Unpause");
		}else{
			sendMpvCommand(PAUSE_COMMAND_SHARED PAUSE_STRING,0);
			setLastAction("Pause");
		}
	}else{
		setLastAction("Failed to get pause status.");
	}
}

void removeNewline(char* _toRemove){
	int _cachedStrlen = strlen(_toRemove);
	if (_cachedStrlen==0){
		return;
	}
	if (_toRemove[_cachedStrlen-1]==0x0A){ // Last char is UNIX newline
		if (_cachedStrlen>=2 && _toRemove[_cachedStrlen-2]==0x0D){ // If it's a Windows newline
			_toRemove[_cachedStrlen-2]='\0';
		}else{ // Well, it's at very least a UNIX newline
			_toRemove[_cachedStrlen-1]='\0';
		}
	}
}

void _lowDrawDivider(int _length, int y){
	char _buff[_length+1];
	memset(_buff,DIVIDERCHAR,_length);
	_buff[_length]='\0';
	mvprintw(y,0,_buff,"%s",_buff);
}
void drawDivider(int y){
	_lowDrawDivider(COLS,y);
}
void drawHalfDivider(int y){
	_lowDrawDivider(COLS/2,y);
}

void seekAbsoluteSeconds(double time){
	char buff[strlen(SEEK_STATUS_ABSOLUTE_FORMAT)+20];
	sprintf(buff,SEEK_STATUS_ABSOLUTE_FORMAT,time);
	setLastAction(buff);

	#if NO_MPV
		_startTime+=time;
	#else
		char complete[strlen(SEEK_ABSOLUTE_FORMAT)+20];
		sprintf(complete,SEEK_ABSOLUTE_FORMAT,time);
		sendMpvCommand(complete,0);
	#endif
}
void seekSeconds(double time){
	char buff[strlen(SEEK_STATUS_FORMAT)+20];
	sprintf(buff,SEEK_STATUS_FORMAT,time);
	setLastAction(buff);

	#if NO_MPV
		_startTime-=time*1000;
	#else
		char complete[strlen(SEEK_FORMAT)+20];
		sprintf(complete,SEEK_FORMAT,time);
		sendMpvCommand(complete,0);
	#endif
}
double getMpvDouble(char* command){
	// Step 1 - Get the info from mpv
	char dresult[256];
	FILE* fp = sendMpvCommand(command,1);
	dresult[fread(dresult,1,sizeof(dresult)-1,fp)]='\0';
	fclose(fp);

	if (strstr(dresult,"success")==NULL){
		return -1;
	}

	char* numStart = strstr(dresult,":");
	char* numEnd = strstr(dresult,",");
	numEnd[0]='\0';
	numStart++;

	return atof(numStart);
}
double getPercent(){
	#if NO_MPV
		return 10.0;
	#else
		return getMpvDouble(GET_PERCENT_STATUS_COMMAND);
	#endif
}
double getSeconds(){
	#if NO_MPV
		return (testMS()-_startTime)/(double)1000;
	#else
		return getMpvDouble(GET_SECONDS_COMMAND);
	#endif
}
int secToMilli(double time){
	return (time-(int)time)*1000;
}
int secToSec(double time){
	return (int)time%60;
}
int secToMin(double time){
	return ((int)time/60)%60;
}
int secToHour(double time){
	return (int)time/60/60;
}
void makeTimestamp(double time, char* buff){
	sprintf(buff,TIMEFORMAT,secToHour(time),secToMin(time),secToSec(time),secToMilli(time));
}

//
void writeSingleSrt(int index, double startTime, double endTime, char* string, FILE* fp){
	char strstampone[strlen(TIMEFORMAT)];
	char strstamptwo[strlen(TIMEFORMAT)];

	makeTimestamp(startTime,strstampone);
	makeTimestamp(endTime,strstamptwo);

	if (strlen(string)==0){ // Can't write blank lines
		string = BLANKLINEREPLACEMENT;
	}

	char complete[strlen(SUBFORMATSTRING)+strlen(strstampone)+strlen(string)+strlen(strstamptwo)+1];
	sprintf(complete,SUBFORMATSTRING,index,strstampone,strstamptwo,string);

	fwrite(complete,strlen(complete),1,fp);
}
//
void addSub(double startTime, double endTime){
	if (currentSubIndex==numRawSubs){
		setLastAction("None left");
		return;
	}
	//_lowAddSub(currentSubIndex+1,startTime,endTime,rawSubs[currentSubIndex]);
	fwrite(&(currentSubIndex),sizeof(int),1,backupFp);
	fwrite(&(startTime),sizeof(double),1,backupFp);
	fwrite(&(endTime),sizeof(double),1,backupFp);
	fflush(backupFp);
	rawStartTimes[currentSubIndex]=startTime;
	rawEndTimes[currentSubIndex]=endTime;
	++currentSubIndex;
}

void drawList(char** list, int y, int numToDraw, int index, int listSize, char reverseOrder){
	if (index+numToDraw>listSize){
		numToDraw = listSize-index;
	}
	if (index<0){
		index=0;
	}
	int i;
	if (reverseOrder){
		for (i=0;i<numToDraw;++i){
			mvprintw(y+numToDraw-i,1,"%s",list[i+index]);
		}
	}else{
		for (i=0;i<numToDraw;++i){
			mvprintw(y+i,1,"%s",list[i+index]);
		}
	}
}

void loadRawsubs(char* filename){
	FILE* fp = fopen(filename,"r");
	size_t _lineSize=0;
	char* _lastLine=NULL;
	numRawSubs=0;
	while (getline(&_lastLine,&_lineSize,fp)!=-1){
		_lineSize=0;
		numRawSubs++;
		rawSubs = realloc(rawSubs,sizeof(char*)*(numRawSubs+1));
		removeNewline(_lastLine);
		rawSubs[numRawSubs-1]=_lastLine;
		_lastLine=NULL;
	}
	rawSubs[numRawSubs]="<end>"; // Prevent 1 overflow

	rawStartTimes = calloc(1,sizeof(double)*numRawSubs);
	rawEndTimes = calloc(1,sizeof(double)*numRawSubs);
	fclose(fp);
}

void _lowSetLastAction(char* _newMessage){
	free(lastAction);
	lastAction = strdup(_newMessage);
}

void resetLastAction(){
	_lowSetLastAction("...");
}

void setLastAction(char* _newMessage){
	_lowSetLastAction(_newMessage);
	lastActionHP = REGULARACTIONHP;
}

void waitMpvStart(){
	while(getSeconds()==-1){
		usleep(100000);
	}
}

//////////////////////////////////

void keyReactAddSub(){
	seekSeconds(-1*REACTIONTIME); // Seek back a bit, account for reaction time
	keyAddSub();
	setLastAction("Start subtitle with reaction time");
}

// Seek to the previous subtitle's end point and set that as the current sub's start point. Your current start point will be discarded if set.
void keySeekPrevEndAndAdd(){
	// Discard old start point
	addingSub=0;

	keySeekPrevEnd();
	keyAddSub();

	setLastAction("Seek to last sub's end and add");
}

// Seek to the end point of last sub
void keySeekPrevEnd(){
	if (addingSub){
		seekAbsoluteSeconds(addSubTime);
		setLastAction("Seek to current sub start");
	}else{
		if (currentSubIndex!=0){
			seekAbsoluteSeconds(rawEndTimes[currentSubIndex-1]);
			setLastAction("Seek to last sub's end");
		}else{
			seekAbsoluteSeconds(0);
			setLastAction("Seek to start");
		}
	}
}

void keyMegaSeekBack(){
	seekSeconds(-1*MEGASEEK);
}

void keyMegaSeek(){
	seekSeconds(MEGASEEK);
}

void keyNormSeekBack(){
	seekSeconds(-1*NORMSEEK);
}

void keyNormSeek(){
	seekSeconds(NORMSEEK);
}

void keyMiniSeekBack(){
	seekSeconds(-1*MINISEEK);
}

void keyMiniSeek(){
	seekSeconds(MINISEEK);
}

void keyQuit(){
	setLastAction("Quit");
	running=0;
}

void keyPause(){
	togglePause();
}

// When you set the start point for the sub incorrectly activate this.
void keyResetSub(){
	setLastAction("Reset current subtitle");
	addingSub=0;
}

void keyBackSub(){
	if (currentSubIndex==0){
		setLastAction("Can't go back further");
		addingSub=0;
	}else{
		setLastAction("Back");
		--currentSubIndex;
		addingSub=1;
		addSubTime = rawStartTimes[currentSubIndex];
	}
}

void keyEndSub(){
	if (addingSub){
		setLastAction("End subtitle");
		addSub(addSubTime,getSeconds());
		addingSub=0;
	}else{
		setLastAction("Can't set sub end point before start point");
	}
}

void keyAddSub(){
	if (addingSub){
		double _currentTime = getSeconds();
		addSub(addSubTime,_currentTime);
		addSubTime = _currentTime;
		setLastAction("Next subtitle (a)");
	}else{
		setLastAction("Start subtitle");
		addingSub=1;
		addSubTime = getSeconds();
	}
}

//////////////////////////////////

void bindKey(int key, keyFunc toBind){
	totalKeysBound++;
	boundFuncs = realloc(boundFuncs, sizeof(keyFunc)*totalKeysBound);
	boundKeys = realloc(boundKeys, sizeof(int)*totalKeysBound);

	boundKeys[totalKeysBound-1] = key;
	boundFuncs[totalKeysBound-1] = toBind;
}

// Please only use this with keys the user inputs. Do not hard code key names.
void runKeyFunc(int key){
	int i;
	for (i=0;i<totalKeysBound;++i){
		if (boundKeys[i]==key){
			boundFuncs[i]();
			break;
		}
	}
}

//
void deinit(){
	// Deinit curses
	delwin(mainwin);
	endwin();
	refresh();

	// This goes here because we open this in init function
	fclose(backupFp);

	#if QUIT_MPV_ON_END && !NO_MPV
		sendMpvCommand("quit",0);
	#endif
}

char init(int numArgs, char** argStr){
	// Init mpv and subs from arguments
	if (numArgs>=3 && numArgs<=4){
		srtOutFilename = strdup(argStr[2]);
		rawOutFilename = malloc(strlen(srtOutFilename)+strlen(BONUSEXTENSION)+1);
			strcpy(rawOutFilename,srtOutFilename);
			strcat(rawOutFilename,BONUSEXTENSION);
		rawSubInFilename = strdup(argStr[1]);

		// Start mpv
		if (numArgs==4){
			char buff[strlen(STARTMPVFORMAT)+strlen(argStr[3])+1];
			sprintf(buff,STARTMPVFORMAT,argStr[3]);
			system(buff);

			audioFilename = strdup(argStr[3]);
		}
		printf("Waiting for mpv with --input-ipc-server "STRMLOC"...\n");
		waitMpvStart();

		loadRawsubs(rawSubInFilename);

		// If the raw output subs already exist, load them up.
		if (fileExist(rawOutFilename)){
			backupFp = fopen(rawOutFilename,"r");
			int _maxReadIndex=-1;
			double _highestEnd=0;
			while (!feof(backupFp)){
				int _lastReadIndex;
				double _lastReadStart;
				double _lastReadEnd;
				
				if (fread(&(_lastReadIndex),sizeof(int),1,backupFp)!=1) break;
				if (fread(&(_lastReadStart),sizeof(double),1,backupFp)!=1) break;
				if (fread(&(_lastReadEnd),sizeof(double),1,backupFp)!=1) break;

				rawStartTimes[_lastReadIndex]=_lastReadStart;
				rawEndTimes[_lastReadIndex]=_lastReadEnd;

				if (_lastReadIndex>_maxReadIndex){
					_maxReadIndex = _lastReadIndex;
				}
				if (_lastReadEnd>_highestEnd){
					_highestEnd = _lastReadEnd;
				}
			}
			currentSubIndex = _maxReadIndex+1;
			if (currentSubIndex==numRawSubs){
				printf("This sub project is already complete.\nWhat would you like to do?\n---\n1 - Reopen this project, but discard the typesetting for the last subtitle.\n2 - Reopen this project, but just regenerate the srt and mka.\n3 - Exit without doing anything.\n---\n");
				int _answer = goodGetC(stdin);
				if (_answer=='1'){
					currentSubIndex--; // Discard last sub
				}else if (_answer=='2'){
					// Leave it as is, it'll automaticlly break from the main loop
				}else{
					return 1; // Exit
				}
			}

			fclose(backupFp);
			backupFp = fopen(rawOutFilename,"a");

			seekAbsoluteSeconds(_highestEnd);
		}else{
			backupFp = fopen(rawOutFilename,"w");
		}
	}else{
		printf("bad num args.\n");
		printf("./a.out <plaintext subs> <srt output filename> [audio file]\n");
		return 1;
	}

	// init curses
	mainwin=initscr();
	noecho();
	cbreak();
	keypad(stdscr, TRUE); // Magically fix arrow keys
	timeout(200); // Only wait 200ms for key input before redraw
	if(has_colors()){
		canColors=1;
	}
	// Init colors
	if (canColors){
		start_color();
		init_pair(COL_ADDINGSUB, COLOR_GREEN, COLOR_BLACK);
	}

	// Init positions now that we know the number of lines and stuff 
	listDrawLength = LINES-listTopPad-listBottomPad;
	if (listDrawLength%2==0){ // If it's even we need to do fixing because we want the cursor centered
		listDrawLength--; // This removes two lines from the total because we recalculate listDrawLength, see below.
		listHalfDrawLength = listDrawLength/2;
		listDrawLength = listHalfDrawLength*2+1; // Two halves plus the cursor middle

		listBottomPad++; // That extra lines we removed goes to the bottom pad
	}else{
		listHalfDrawLength = listDrawLength/2;
	}
	drawCursorY = listHalfDrawLength+listTopPad;

	setLastAction("Welcome");

	// Sub keybinds
	bindKey('a',keyAddSub);
	bindKey('d',keyReactAddSub);
	bindKey('s',keyEndSub);
	bindKey('z',keyBackSub);
	bindKey('x',keyResetSub);
	// Seek keybinds
	bindKey(KEY_LEFT,keyMegaSeekBack);
	bindKey(KEY_RIGHT,keyMegaSeek);
	bindKey('q',keyMiniSeekBack);
	bindKey('w',keyMiniSeek);
	bindKey('1',keyNormSeekBack);
	bindKey('2',keyNormSeek);
	bindKey('`',keySeekPrevEnd);
	// Other keybinds
	bindKey('~',keySeekPrevEndAndAdd);
	bindKey(KEY_END,keyQuit);
	bindKey(' ',keyPause);

	return 0;
}

int main(int numArgs, char** argStr){
	if (init(numArgs,argStr)){
		return 1;
	}

	while (currentSubIndex!=numRawSubs && running){
		// Process
		char _timestampBuff[strlen(TIMEFORMAT)];
		makeTimestamp(getSeconds(),_timestampBuff);

		// Draw
		erase();
		///////////////
		mvprintw(0,0,"Minimal Typesetting");
		drawDivider(1);
		//
		if (currentSubIndex<listHalfDrawLength){
			drawList(rawSubs,drawCursorY-currentSubIndex,listHalfDrawLength+1+currentSubIndex,0,numRawSubs,0);
		}else{
			drawList(rawSubs,listTopPad,listDrawLength,currentSubIndex-listHalfDrawLength,numRawSubs,0);
		}

		// Clear the line with our next subtitle on it because we'll do special drawing on it
		move(drawCursorY,0);
		clrtoeol();

		move(drawCursorY,2); // It's always indented
		if (addingSub){
			if (canColors){
				attron(COLOR_PAIR(COL_ADDINGSUB));
				printw(rawSubs[currentSubIndex]);
				attroff(COLOR_PAIR(COL_ADDINGSUB));
			}else{
				move(drawCursorY,5); // Indent more if we're not using colors
				printw(rawSubs[currentSubIndex]);
			}
		}else{
			printw(rawSubs[currentSubIndex]);
		}

		mvaddch(drawCursorY,0,'>');
		//
		drawDivider(LINES-listBottomPad);
		mvprintw(LINES-listBottomPad+1,0,"%s",lastAction);
		mvprintw(LINES-listBottomPad+2,0,_timestampBuff);
		addch(' '); // Skip a space before drawing percent
		printw(PERCENTFORMAT,getPercent());
		///////////////
		refresh();

		// Check inputs
		int _nextInput = getch();
		if (_nextInput!=ERR){
			runKeyFunc(_nextInput);
		}

		// Your last action is only displayed for so long
		if (lastActionHP!=0){
			--lastActionHP;
			if (lastActionHP==0){
				resetLastAction();
			}
		}
	}
	

	//
	printf("Writing srt...\n");
	FILE* outfp = fopen(srtOutFilename,"w");
	int i;
	for (i=0;i<currentSubIndex;++i){
		writeSingleSrt(i+1,rawStartTimes[i],rawEndTimes[i],rawSubs[i],outfp);
	}
	fclose(outfp);

	//
	#if CREATE_MKA_ON_END
		if (audioFilename!=NULL){
			// Visual prompt
			erase();
			mvprintw(0,0,"Make mka file to package audio and subs? (y/n)");
			move(1,0);
			refresh();
			// Get key
			int _nextInput;
			do{
				_nextInput = getch();
			}while(_nextInput==ERR);
			// We don't need curses anymore
			deinit();
			// Do
			if (_nextInput=='Y' || _nextInput=='y'){
				char mkaOutFilename[strlen(audioFilename)+strlen(".mka")+1];
				strcpy(mkaOutFilename,audioFilename);
				strcat(mkaOutFilename,".mka");

				char buff[strlen(MAKEMKACOMMAND)+strlen(audioFilename)+strlen(srtOutFilename)+strlen(mkaOutFilename)+1];
				sprintf(buff,MAKEMKACOMMAND,mkaOutFilename,audioFilename,srtOutFilename);
				
				printf("%s\n",buff);
				system(buff);
			}
		}
	#else
		deinit();
	#endif
}