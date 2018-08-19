#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curses.h>

//https://mpv.io/manual/master/#list-of-input-commands

///////////////////////////////////////

#define STRMLOC "/tmp/a"
#define SUBFORMATSTRING "%d\n%s --> %s\n%s\n\n"
#define TIMEFORMAT "%02d:%02d:%02d,%03d"

#define SEEK_FORMAT "echo no-osd seek %f exact | socat - "STRMLOC

int listTopPad = 2; // title, divider
int listBottomPad = 3; // divider, last action, timestamp

int listDrawLength;
int listHalfDrawLength;

#define DIVIDERCHAR '-'

///////////////////////////////////////
char** rawSubs=NULL;
int numRawSubs;

FILE* outfp=NULL;

int totalAppliedSubs=0;

char* lastAction="Welcome";
int lastActionHP=0;
signed char _makeLengthOdd=0;
int drawCursorY;

WINDOW* mainwin;
///////////////////////////////////////

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

void seekSeconds(double time){
	char complete[strlen(SEEK_FORMAT)+3];
	sprintf(complete,SEEK_FORMAT,time);
	system(complete);
}

double getSeconds(){
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
	++totalAppliedSubs;
	
	char strstampone[strlen(TIMEFORMAT)];
	char strstamptwo[strlen(TIMEFORMAT)];

	makeTimestamp(startTime,strstampone);
	makeTimestamp(endTime,strstamptwo);

	char complete[strlen(SUBFORMATSTRING)+strlen(strstampone)+strlen(string)+strlen(strstamptwo)+1];
	sprintf(complete,SUBFORMATSTRING,totalAppliedSubs,strstampone,strstamptwo,string);

	fwrite(complete,strlen(complete),1,outfp);
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
	fclose(fp);
}

void resetLastAction(){
	lastAction="...";
}

void setLastAction(char* _newMessage){
	lastAction = _newMessage;
	lastActionHP = 5;
}

void init(){
	// init curses
	mainwin =initscr();
	noecho();
	cbreak();
	timeout(200); // Only wait 200ms for key input before redraw

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

	// Init mpv and subs from arguments
	/*
	if (numArgs>=3 && numArgs<=4){
		loadRawsubs(argStr[1]);
		
		outfp = fopen(argStr[2],"w");
				
		// Start mpv
		if (numArgs==4){
		}
	}*/
	loadRawsubs("./testraw");
}

// <in raw subs file> <out subs file>
int main(int numArgs, char** argStr){
	init();

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
		if (totalAppliedSubs<listHalfDrawLength){
			drawList(rawSubs,drawCursorY-totalAppliedSubs,listHalfDrawLength+1+totalAppliedSubs,0,numRawSubs,0);
		}else{
			drawList(rawSubs,listTopPad,listDrawLength,totalAppliedSubs-listHalfDrawLength,numRawSubs,0);
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
			setLastAction("Append");
			totalAppliedSubs++;
		}else if (_nextInput=='q'){
			setLastAction("Quit");
			break;
		}else if (_nextInput=='z'){
			setLastAction("Back");
		}else if (_nextInput==KEY_LEFT){
		}else if (_nextInput==KEY_RIGHT){
		}else if (_nextInput==ERR){
			// No input
		}

		/*
		if (getc(stdin)=='\n'){
			if (totalAppliedSubs>=numRawSubs){
				printf("Out of subs! %d/%d\n",totalAppliedSubs,numRawSubs);
			}else{
				double _newTime = getSeconds();
				addSub(lastTime,_newTime,rawSubs[totalAppliedSubs]);
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
	// Deinit curses
	delwin(mainwin);
    endwin();
    refresh();

    system("echo quit | socat - "STRMLOC);

}