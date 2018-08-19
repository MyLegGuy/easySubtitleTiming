//https://mpv.io/manual/master/#list-of-input-commands
// TODO - I need a key to go back to the previous subtitle so I can change where it ends if I cut it off too early.
	// And also the ability to go back even more.
// TODO - Don't just write sub output as I go. This makes it hard to seek back. Instead, use arrays (rawStartTimes, rawEndTimes) to store the times and do the file writing at the end.
	// This may be a bad idea. If the program crashes all progress is lost. Maybe I should write the timestamps to file (in my own format) just as a backup. My custom format can allow you to have multiple timestamps for one sub where whatever timestamp found in the file last is the one used, so even if I seek back and need to write a new timestamp everything's okay.
		// This also allows me to have an easy format I can use for loading up the subs for editing again, I won't even have to me .srt parser

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curses.h>

#include "main.h"

#define NO_MPV 0
#define QUIT_MPV_ON_END 1

#define COL_OLDSUB 2 // Sub has already been added
#define COL_ADDINGSUB 1 // You've selected the sub start point
#define COL_FUTURESUB 3

///////////////////////////////////////

#define STRMLOC "/tmp/a"
#define SUBFORMATSTRING "%d\n%s --> %s\n%s\n\n"
#define TIMEFORMAT "%02d:%02d:%02d,%03d"

#define MPV_MESSAGE_FORMAT "echo %s | socat - "STRMLOC
#define SEEK_FORMAT "no-osd seek %f exact"

#define REDIRECTOUTPUT " > /dev/null"

#define STARTMPVFORMAT "mpv --really-quiet --input-ipc-server /tmp/a --no-input-terminal "STRMLOC" %s & disown"

#define DIVIDERCHAR '-'

#define PAUSE_COMMAND_SHARED "\'{ \"command\": [\"set_property\", \"pause\", "
#define PAUSE_STRING "true] }\'"
#define UNPAUSE_STRING "false] }\'"

#define LRSEEK .5
#define QWSEEK 1

///////////////////////////////////////
char** rawSubs=NULL;
int numRawSubs;
double* rawStartTimes=NULL;
double* rawEndTimes=NULL;

FILE* outfp=NULL;

char* lastAction=NULL;
int lastActionHP=0;
signed char _makeLengthOdd=0;
int drawCursorY;

WINDOW* mainwin;

int listTopPad = 2; // title, divider
int listBottomPad = 3; // divider, last action, timestamp

int listDrawLength;
int listHalfDrawLength;

char addingSub=0;
int currentSubIndex=0;
char isPaused=0;
///////////////////////////////////////

#if NO_MPV
	#include <time.h>
	long _startTime=0;
	long testMS(){
		struct timespec _myTime;
		clock_gettime(CLOCK_MONOTONIC, &_myTime);
		return _myTime.tv_nsec/1000000+_myTime.tv_sec*1000;
	}
#endif

void sendMpvCommand(char* msg){
	char buff[strlen(MPV_MESSAGE_FORMAT)+strlen(msg)+strlen(REDIRECTOUTPUT)];
	sprintf(buff,MPV_MESSAGE_FORMAT,msg);
	strcat(buff,REDIRECTOUTPUT); // Apply output redirection
	system(buff);
}


void togglePause(){
	if (isPaused){
		sendMpvCommand(PAUSE_COMMAND_SHARED UNPAUSE_STRING);
		isPaused=0;
		setLastAction("Unpause");
	}else{
		sendMpvCommand(PAUSE_COMMAND_SHARED PAUSE_STRING);
		isPaused=1;
		setLastAction("Pause");
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

#define SEEK_STATUS_FORMAT "Seek %f"
void seekSeconds(double time){
	char buff[strlen(SEEK_STATUS_FORMAT)+20];
	sprintf(buff,SEEK_STATUS_FORMAT,time);
	setLastAction(buff);

	#if NO_MPV
		_startTime-=time*1000;
	#else
		char complete[strlen(SEEK_FORMAT)+3];
		sprintf(complete,SEEK_FORMAT,time);
		sendMpvCommand(complete);
	#endif
}

double getSeconds(){
	#if NO_MPV
		return (testMS()-_startTime)/(double)1000;
	#else
		// Step 1 - Get the info from mpv
		char dresult[256];
		FILE* fp = popen("echo \'{ \"command\": [\"get_property\", \"playback-time\"] }\' | socat - "STRMLOC,"r");
		fread(dresult,sizeof(dresult),1,fp);
		fclose(fp);
		// Parse info
		//{"data":434.476000,"error":"success"}
		char* numStart = strstr(dresult,":");
		char* numEnd = strstr(dresult,",");
		numEnd[0]='\0';
		numStart++;
		//
		return atof(numStart);
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
void addSub(double startTime, double endTime, char* string){
	++currentSubIndex;
	
	char strstampone[strlen(TIMEFORMAT)];
	char strstamptwo[strlen(TIMEFORMAT)];

	makeTimestamp(startTime,strstampone);
	makeTimestamp(endTime,strstamptwo);

	char complete[strlen(SUBFORMATSTRING)+strlen(strstampone)+strlen(string)+strlen(strstamptwo)+1];
	sprintf(complete,SUBFORMATSTRING,currentSubIndex,strstampone,strstamptwo,string);

	fwrite(complete,strlen(complete),1,outfp);


	if (currentSubIndex==numRawSubs){
		printf("Done!\n");
		deinit();
		exit(0);
	}
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
		rawSubs = realloc(rawSubs,sizeof(char*)*numRawSubs);
		removeNewline(_lastLine);
		rawSubs[numRawSubs-1]=_lastLine;
		_lastLine=NULL;
	}

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
	lastActionHP = 5;
}

void deinit(){
	// Deinit curses
	delwin(mainwin);
	endwin();
	refresh();

	#if QUIT_MPV_ON_END && !NO_MPV
		sendMpvCommand("quit");
	#endif
}

void init(int numArgs, char** argStr){
	// Init mpv and subs from arguments
	if (numArgs==1){
		outfp = fopen("./testout","w");
		loadRawsubs("./testraw");
	}else if (numArgs>=3 && numArgs<=4){
		loadRawsubs(argStr[1]);
		
		outfp = fopen(argStr[2],"w");
				
		// Start mpv
		if (numArgs==4){
			char buff[strlen(STARTMPVFORMAT)+strlen(argStr[3])+1];
			sprintf(buff,STARTMPVFORMAT,argStr[3]);
			system(buff);
			// Make sure ipc server gets open by waiting for mpv
			sleep(1);
		}
	}else{
		printf("bad num args.");
		exit(0);
	}

	// init curses
	mainwin=initscr();
	noecho();
	cbreak();
	keypad(stdscr, TRUE); // Magically fix arrow keys
	timeout(200); // Only wait 200ms for key input before redraw
	if(has_colors() == 0){
		endwin();
		printf("You don't have color.\n");
		exit(1);
	}
	// Init colors
	start_color();
	init_pair(COL_ADDINGSUB, COLOR_GREEN, COLOR_BLACK);
	init_pair(COL_OLDSUB, COLOR_BLUE, COLOR_BLACK);
	init_pair(COL_FUTURESUB, COLOR_YELLOW, COLOR_BLACK);

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

	#if NO_MPV
		_startTime = testMS();
	#endif
}

// <in raw subs file> <out subs file>
int main(int numArgs, char** argStr){
	init(numArgs,argStr);

	double addSubTime;

	//double lastTime = getSeconds();
	while (1){
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
			// History
			attron(COLOR_PAIR(COL_OLDSUB));
			drawList(rawSubs,listTopPad,listHalfDrawLength,currentSubIndex-listHalfDrawLength,numRawSubs,0);
			attroff(COLOR_PAIR(COL_OLDSUB));

			// Future
			attron(COLOR_PAIR(COL_FUTURESUB));
			drawList(rawSubs,listTopPad+listHalfDrawLength+1,listHalfDrawLength,currentSubIndex+1,numRawSubs,0);
			attroff(COLOR_PAIR(COL_FUTURESUB));
		}

		// Clear the line with our next subtitle on it because we'll do special drawing on it
		move(drawCursorY,0);
		clrtoeol();

		move(drawCursorY,2); // It's always indented
		if (addingSub){
			attron(COLOR_PAIR(COL_ADDINGSUB));
			printw(rawSubs[currentSubIndex]);
			attroff(COLOR_PAIR(COL_ADDINGSUB));
		}else{
			printw(rawSubs[currentSubIndex]);
		}

		mvaddch(drawCursorY,0,'>');
		//
		drawDivider(LINES-listBottomPad);
		mvprintw(LINES-listBottomPad+1,0,"%s",lastAction);
		mvprintw(LINES-listBottomPad+2,0,_timestampBuff);
		///////////////
		refresh();

		// Check inputs
		int _nextInput = getch();
		if (_nextInput=='a'){
			if (addingSub){
				double _currentTime = getSeconds();
				addSub(addSubTime,_currentTime,rawSubs[currentSubIndex]);
				addSubTime = _currentTime;
				setLastAction("Next subtitle (a)");
			}else{
				setLastAction("Start subtitle");
				addingSub=1;
				addSubTime = getSeconds();
			}
		}else if (_nextInput=='s'){
			setLastAction("End subtitle");
			addSub(addSubTime,getSeconds(),rawSubs[currentSubIndex]);
			addingSub=0;
		}else if (_nextInput=='n'){
			setLastAction("Next subtitle.");
		}else if (_nextInput==KEY_END){
			setLastAction("Quit");
			break;
		}else if (_nextInput=='z'){
			setLastAction("Back");
		}else if (_nextInput==KEY_LEFT){
			seekSeconds(-1*LRSEEK);
		}else if (_nextInput==KEY_RIGHT){
			seekSeconds(LRSEEK);
		}else if (_nextInput=='q'){
			seekSeconds(-1*QWSEEK);
		}else if (_nextInput=='w'){
			seekSeconds(QWSEEK);
		}else if (_nextInput==' '){
			togglePause();
		}else if (_nextInput==ERR){
			// No input
		}

		/*
		if (getc(stdin)=='\n'){
			if (currentSubIndex>=numRawSubs){
				printf("Out of subs! %d/%d\n",currentSubIndex,numRawSubs);
			}else{
				double _newTime = getSeconds();
				addSub(lastTime,_newTime,rawSubs[currentSubIndex]);
				lastTime = _newTime;
			}
		}else{
			break;
		}
		*/
		if (lastActionHP!=0){
			--lastActionHP;
			if (lastActionHP==0){
				resetLastAction();
			}
		}
	}
	deinit();
}