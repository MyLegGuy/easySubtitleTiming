void _lowDrawDivider(int _length, int y);
void _lowSetLastAction(char* _newMessage);
void addSub(double startTime, double endTime);
void drawDivider(int y);
void drawHalfDivider(int y);
void drawList(char** list, int y, int numToDraw, int index, int listSize, char reverseOrder);
double getSeconds();
char init(int numArgs, char** argStr);
void loadRawsubs(char* filename);
int main(int numArgs, char** argStr);
void makeTimestamp(double time, char* buff);
void removeNewline(char* _toRemove);
void resetLastAction();
int secToHour(double time);
int secToMilli(double time);
int secToMin(double time);
int secToSec(double time);
void seekSeconds(double time);
void setLastAction(char* _newMessage);
void deinit();