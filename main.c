//https://mpv.io/manual/master/#list-of-input-commands

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curses.h>
#include <unistd.h>

#include "main.h"

#define NO_MPV 0
#define QUIT_MPV_ON_END 1
#define CREATE_MKA_ON_END 1

// Color indices
#define COL_ADDINGSUB 1 // You've selected the sub start point

///////////////////////////////////////

#define STRMLOC "/tmp/mpvtypeset"
#define SUBFORMATSTRING "%d\n%s --> %s\n%s\n\n"
#define TIMEFORMAT "%02d:%02d:%02d,%03d"

#define MPV_MESSAGE_FORMAT "echo %s | socat - "STRMLOC
#define SEEK_FORMAT "no-osd seek %f exact"
#define SEEK_ABSOLUTE_FORMAT "no-osd seek %f absolute"

#define REDIRECTOUTPUT " > /dev/null"

#define STARTMPVFORMAT "mpv --keep-open=yes --really-quiet --input-ipc-server "STRMLOC" --no-input-terminal "STRMLOC" %s & disown"

// output mka, in audio file, in sub file
#define MAKEMKACOMMAND "mkvmerge -o %s %s %s"

#define DIVIDERCHAR '-'

#define PAUSE_COMMAND_SHARED "\'{ \"command\": [\"set_property\", \"pause\", "
#define PAUSE_STRING "true] }\'"
#define UNPAUSE_STRING "false] }\'"

#define BONUSEXTENSION ".rawPos"

#define LRSEEK 1
#define ONETWOSEEK 3
#define QWSEEK .5

#define SEEK_STATUS_FORMAT "Seek %f"
#define SEEK_STATUS_ABSOLUTE_FORMAT "Seek to %f"

///////////////////////////////////////
char** rawSubs=NULL;
int numRawSubs;
double* rawStartTimes=NULL;
double* rawEndTimes=NULL;

FILE* backupFp=NULL;

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

char canColors=0;

char* audioFilename=NULL;
char* rawSubInFilename=NULL;
char* srtOutFilename=NULL;
char* rawOutFilename=NULL;
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

char fileExist(char* filename){
	FILE* fp = fopen(filename,"r");
	if (fp!=NULL){
		fclose(fp);
		return 1;
	}
	return 0;
}

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

void seekAbsoluteSeconds(double time){
	char buff[strlen(SEEK_STATUS_ABSOLUTE_FORMAT)+20];
	sprintf(buff,SEEK_STATUS_ABSOLUTE_FORMAT,time);
	setLastAction(buff);

	#if NO_MPV
		_startTime+=time;
	#else
		char complete[strlen(SEEK_ABSOLUTE_FORMAT)+20];
		sprintf(complete,SEEK_ABSOLUTE_FORMAT,time);
		sendMpvCommand(complete);
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
		if (strstr(dresult,"success")==NULL){
			return -1;
		}
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
void writeSingleSrt(int index, double startTime, double endTime, char* string, FILE* fp){
	char strstampone[strlen(TIMEFORMAT)];
	char strstamptwo[strlen(TIMEFORMAT)];

	makeTimestamp(startTime,strstampone);
	makeTimestamp(endTime,strstamptwo);

	char complete[strlen(SUBFORMATSTRING)+strlen(strstampone)+strlen(string)+strlen(strstamptwo)+1];
	sprintf(complete,SUBFORMATSTRING,currentSubIndex,strstampone,strstamptwo,string);

	fwrite(complete,strlen(complete),1,fp);
}
//
void addSub(double startTime, double endTime){
	if (currentSubIndex==numRawSubs){
		setLastAction("None left.");
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
	lastActionHP = 5;
}

void waitMpvStart(){
	while(getSeconds()==-1){
		usleep(100000);
	}
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

// 
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
		printf("Waiting for mpv with socket "STRMLOC"...");
		waitMpvStart();

		loadRawsubs(rawSubInFilename);

		// If the raw output subs already exist, load them up.
		if (fileExist(rawOutFilename)){
			backupFp = fopen(rawOutFilename,"r");
			int _maxReadIndex=0;
			double _highestEnd=0;
			while (!feof(backupFp)){
				int _lastReadIndex;
				double _lastReadStart;
				double _lastReadEnd;
				
				fread(&(_lastReadIndex),sizeof(int),1,backupFp);
				fread(&(_lastReadStart),sizeof(double),1,backupFp);
				fread(&(_lastReadEnd),sizeof(double),1,backupFp);

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
				--currentSubIndex;
			}

			fclose(backupFp);
			backupFp = fopen(rawOutFilename,"a");

			seekAbsoluteSeconds(_highestEnd);
		}else{
			backupFp = fopen(rawOutFilename,"w");
		}
	}else{
		printf("bad num args.");
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
	return 0;
}

// <in raw subs file> <out subs file>
int main(int numArgs, char** argStr){
	if (init(numArgs,argStr)){
		return 1;
	}

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
		///////////////
		refresh();

		// Check inputs
		int _nextInput = getch();

		if (_nextInput!=ERR){
			if (_nextInput=='d'){ // Acts as if you pressed q and then a. Point is to account for human reaction time.
				seekSeconds(-1*QWSEEK); // Do the 'q'
				_nextInput='a'; // Trigger the 'a' input
			}
			if (_nextInput=='a'){ // Intentionally not else-if
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
			}else if (_nextInput=='s'){
				setLastAction("End subtitle");
				addSub(addSubTime,getSeconds());
				addingSub=0;
			}else if (_nextInput=='z'){ // Like ^Z
				if (currentSubIndex==0){
					setLastAction("Can't go back further");
					addingSub=0;
				}else{
					setLastAction("Back");
					--currentSubIndex;
					addingSub=1;
					addSubTime = rawStartTimes[currentSubIndex];
				}
			}else if (_nextInput=='x'){ // Stands for "excape from this sub, I don't want to add it anymore!"
				addingSub=0;
			}else if (_nextInput==' '){
				togglePause();
			}else if (_nextInput==KEY_END){
				setLastAction("Quit");
				break;
			}else if (_nextInput==KEY_LEFT){
				seekSeconds(-1*LRSEEK);
			}else if (_nextInput==KEY_RIGHT){
				seekSeconds(LRSEEK);
			}else if (_nextInput=='q'){
				seekSeconds(-1*QWSEEK);
			}else if (_nextInput=='w'){
				seekSeconds(QWSEEK);
			}else if (_nextInput=='1'){
				seekSeconds(-1*ONETWOSEEK);
			}else if (_nextInput=='2'){
				seekSeconds(ONETWOSEEK);
			}else if (_nextInput=='`'){ // Seek back to previous subtitle's end or current subtitle's start
				if (addingSub){
					seekAbsoluteSeconds(addSubTime);
					setLastAction("Seek to current sub start");
				}else{
					if (currentSubIndex!=0){
						seekAbsoluteSeconds(rawEndTimes[currentSubIndex-1]);
						setLastAction("Seek to last sub's end");
					}else{
						setLastAction("Can't do that!");
					}
				}
			}
		}

		//
		// Quit if we're now on the last sub. If we don't exit now we'll crash later trying to draw too far in the array
		if (currentSubIndex==numRawSubs){
			break;
		}
		// Your last action is only displayed for so long
		if (lastActionHP!=0){
			--lastActionHP;
			if (lastActionHP==0){
				resetLastAction();
			}
		}
	}
	deinit();

	//
	printf("Writing srt...\n");
	FILE* outfp = fopen(srtOutFilename,"w");
	int i;
	for (i=0;i<currentSubIndex;++i){
		writeSingleSrt(i+1,rawStartTimes[i],rawEndTimes[i],rawSubs[i],outfp);
	}

	//
	#if CREATE_MKA_ON_END
		if (audioFilename!=NULL){
			printf("Make mka file to package audio and subs?\n(y/n): ");
			int _answer = getc(stdin);
			if (_answer=='Y' || _answer=='y'){
				char mkaOutFilename[strlen(audioFilename)+strlen(".mka")+1];
				strcpy(mkaOutFilename,audioFilename);
				strcat(mkaOutFilename,".mka");

				char buff[strlen(MAKEMKACOMMAND)+strlen(audioFilename)+strlen(srtOutFilename)+strlen(mkaOutFilename)+1];
				sprintf(buff,MAKEMKACOMMAND,mkaOutFilename,audioFilename,srtOutFilename);

				system(buff);
			}
		}
	#endif
}