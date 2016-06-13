using namespace std;
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "imdb.h"

const char *const imdb::kActorFileName = "actordata";
const char *const imdb::kMovieFileName = "moviedata";

/* Type Definitions */

typedef struct keyData{
    const void* pattern;
    const void* file;
} keyData;

/* Function Prototypes */

static string getActorData(const void* file, const void* offset);
static short setCursor(const string& elem, void*& cursor, bool movie);
static film getMovieData(const void* file, const void* offset);
static void* applyByteOffset(const void* file, const void* offset);
static int actorCmp(const void* key, const void* elem);
static int movieCmp(const void* key, const void* elem);
static void* searchFile(const void* file, const void* pattern, int (*cmpFn) (const void*, const void*));

/* Method Implementations */

imdb::imdb(const string& directory)
{
  const string actorFileName = directory + "/" + kActorFileName;
  const string movieFileName = directory + "/" + kMovieFileName;
  
  actorFile = acquireFileMap(actorFileName, actorInfo);
  movieFile = acquireFileMap(movieFileName, movieInfo);
}

bool imdb::good() const
{
  return !( (actorInfo.fd == -1) || 
	    (movieInfo.fd == -1) ); 
}

// you should be implementing these two methods right here... 
bool imdb::getCredits(const string& player, vector<film>& films) const { 
    void* offset = searchFile(actorFile, &player, actorCmp);
    if (offset == NULL) return false;
    void* cursor = applyByteOffset(actorFile, offset);
    short numCredits = setCursor(player, cursor, false);
    for (short i = 0; i < numCredits; i++) {
        film credit = getMovieData(movieFile, (int*) cursor + i);
        films.push_back(credit);
    }
    return true;
}

 
bool imdb::getCast(const film& movie, vector<string>& players) const { 
    void* offset = searchFile(movieFile, &movie, movieCmp);
    if (offset == NULL) return false;
    void* cursor = applyByteOffset(movieFile, offset);
    short numActors = setCursor(movie.title, cursor, true);
    for (short i = 0; i < numActors; i++) {
        string actor = getActorData(actorFile, (int*) cursor + i);
        players.push_back(actor);
    }
    return true; 
}

imdb::~imdb()
{
  releaseFileMap(actorInfo);
  releaseFileMap(movieInfo);
}

// ignore everything below... it's all UNIXy stuff in place to make a file look like
// an array of bytes in RAM.. 
const void *imdb::acquireFileMap(const string& fileName, struct fileInfo& info)
{
  struct stat stats;
  stat(fileName.c_str(), &stats);
  info.fileSize = stats.st_size;
  info.fd = open(fileName.c_str(), O_RDONLY);
  return info.fileMap = mmap(0, info.fileSize, PROT_READ, MAP_SHARED, info.fd, 0);
}

void imdb::releaseFileMap(struct fileInfo& info)
{
  if (info.fileMap != NULL) munmap((char *) info.fileMap, info.fileSize);
  if (info.fd != -1) close(info.fd);
}

/* Private Methods */

/**
 * Method: setCursor
 * Usage: (char*) actor = setCursor(const string name, 
 * ------------------------------------------------------
 *
 */


/* Function Definitions */

static short setCursor(const string& elem, void*& cursor, bool movie)
{
    int numBytes = elem.length() + 1;
    if (movie == true) numBytes++;
    if (numBytes % 2 != 0) numBytes++;
    cursor = (char*) cursor + numBytes;
    short s = *(short*) cursor;
    if ((numBytes + 2) % 4 == 0) cursor = (char*) cursor + 2;
    else cursor = (char*) cursor + 4;
    return s;
}


/**
 * Function: applyOffset
 * Usage: char* pattern = applyOffset(file, offset);
 * -------------------------------------------------
 * Function that dereferences an int* representing the offset in bytes from the start of a file
 * one must look to find the start of pattern to match. bsearch is runs on metadata, so this
 * function is called by a comparison functions in to locate the character data to be compared
 */

static void* applyByteOffset(const void* file, const void* offset)
{
    return (char*) file + *(int*) offset;
}

static film getMovieData(const void* file, const void* offset)
{
    film movie;
    char* titlePtr = (char*) applyByteOffset(file, offset);
    while (*titlePtr != '\0') movie.title += *titlePtr++;
    titlePtr++;
    movie.year = (int) (*titlePtr) + 1900;
    return movie;
}

static string getActorData(const void* file, const void* offset)
{
    string actor;
    char* name = (char*) applyByteOffset(file, offset);
    while (*name != '\0') actor += *name++;
    return actor;
}

/**
 * Function: movieCmp
 * Usage: int matchCode = movieCmp(key, elem);
 * -------------------------------------------
 */

static int movieCmp(const void* key, const void* elem)
{
    keyData* data = (keyData*) key;
    film movieKey = *((film*) data->pattern);
    film movie = getMovieData(data->file, elem);
    if (movieKey < movie) return -1;
    else if (!(movieKey < movie) && !(movieKey == movie)) return 1;
    else return 0;
}


/**
 * Function: actorCmp
 * Usage: int matchCode = actorCmp(key, elem);
 * -------------------------------------------
 * Comparison function that takes a record containing the name of an actor and a pointer to the
 * element in metadata that bsearch points to. The element represents the offset from the end of
 * the metadata to the end of the record to be matched. A separate function is called to supply the
 * start of the record so that it can also be used for the movieCmp function, and then strcmp is
 * called to compare the supplied actor name with the name supplied by the record being searched.
 */

static int actorCmp(const void* key, const void* elem)
{
    keyData* data = (keyData*) key;
    char* elemName = (char*) applyByteOffset(data->file, elem);
    return strcmp((*(string*) (data->pattern)).c_str(), elemName);
}


/**
 * Function: searchFile
 * Usage: void* match = searchFile(file, pattern, cmpFn)
 * -----------------------------------------------------
 * Function that takes file to be searched, pattern to be matched, and comparison function.
 * Calculates the number of records by dereferencing initial sequence of bytes cast as int pointer
 * Sets base of bsearch to begin at point in file located an int's worth of bytes from start
 * Populates keyData struct with pattern to be matched and file to be crawled
 * Returns void* to location in file where match occurs to be cast when function is utilized
 * by wrapper function or method.
 */
static void* searchFile(const void* file, const void* pattern, int (*cmpFn) (const void*, const void*))
{
    int numRecords = *(int*) file;
    int *base = (int*) file + 1;
    keyData data;
    data.pattern = pattern;
    data.file = file;
    return bsearch(&data, base, numRecords, sizeof(int), cmpFn);
}
