typedef void(*keyFunc)();

void _lowDrawDivider(int _length, int y);
void _lowSetLastAction(char* _newMessage);
void addSub(double startTime, double endTime);
void bindKey(int key, keyFunc toBind);
void deinit();
void drawDivider(int y);
void drawHalfDivider(int y);
void drawList(char** list, int y, int numToDraw, int index, int listSize, char reverseOrder);
char fileExist(char* filename);
double getSeconds();
int goodGetC(FILE* fp);
char init(int numArgs, char** argStr);
void keyAddSub();
void keyBackSub();
void keyEndSub();
void keyMegaSeekBack();
void keyMegaSeek();
void keyMiniSeekBack();
void keyMiniSeek();
void keyNormSeekBack();
void keyNormSeek();
void keyPause();
void keyQuit();
void keyReactAddSub();
void keyResetSub();
void keySeekPrevEnd();
void loadRawsubs(char* filename);
int main(int numArgs, char** argStr);
void makeTimestamp(double time, char* buff);
void removeNewline(char* _toRemove);
void resetLastAction();
void runKeyFunc(int key);
int secToHour(double time);
int secToMilli(double time);
int secToMin(double time);
int secToSec(double time);
void seekAbsoluteSeconds(double time);
void seekSeconds(double time);
void sendMpvCommand(char* msg);
void setLastAction(char* _newMessage);
long testMS();
void togglePause();
void waitMpvStart();
void writeSingleSrt(int index, double startTime, double endTime, char* string, FILE* fp);