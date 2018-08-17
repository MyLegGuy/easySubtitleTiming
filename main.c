#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curses.h>

///////////////////////////////////////

#define STRMLOC "/tmp/a"
#define SUBFORMATSTRING "%d\n%s --> %s\n%s\n\n"
#define TIMEFORMAT "%02d:%02d:%02d,%03d"

///////////////////////////////////////
char** rawSubs=NULL;
int numRawSubs;

FILE* outfp=NULL;

int totalAppliedSubs=0;
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

#define SEEK_FORMAT "echo seek %d relative | socat - "STRMLOC
void seekSeconds(signed int time){
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

// <in raw subs file> <out subs file>
int main(int numArgs, char** argStr){
	// init curses
	WINDOW* mainwin =initscr();
	noecho();
	cbreak();

	if (numArgs>=3 && numArgs<=4){
		FILE* fp = fopen(argStr[1],"r");
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
		
		outfp = fopen(argStr[2],"w");
				
		// Start mpv
		if (numArgs==4){
		}
	}
	printf("%d;%d\n",COLS,LINES);
	//double lastTime = getSeconds();
	while (1){
		/*
		UI Idea:
			-----------------
			SUBS TO ADD (a list of the next sub you'll add, with the arrow pointing to the next one. Maybe don't show any subs that you've already added in this list because they'll be in SUB LOG)
			-------
			sub1
			sub2
		  > sub3
			sub4
			sub5
			-----------------
			SUB LOG (subs you've added)
			-------
			sub -3
			sub -2
			sub -1
			sub 1
			sub 2
			-----------------
			Last action
			-------
			[Append subtitle]
			-----------------
			Timestamp
			-------
			00:00:00
			-----------------
		*/

		erase();
		int i;
		for (i=0;i<LINES;++i){
			mvprintw(i,0,"%s:%d","bla",i);
		}
		refresh();

		int _nextInput = getch();
		if (_nextInput=='a'){
			printf("Append.\n");
		}else if (_nextInput=='q'){
			printf("Quit.\n");
			break;
		}else if (_nextInput=='z'){
			printf("undo.\n");
		}else if (_nextInput==KEY_LEFT){
		}else if (_nextInput==KEY_RIGHT){
		}else if (_nextInput==ERR){
			printf("getch error.\n");
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
	}
	// Deinit curses
	delwin(mainwin);
    endwin();
    refresh();

    system("echo quit | socat - "STRMLOC);

}