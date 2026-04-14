#include <errno.h>
#include <fcntl.h>
#include <ndbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *db_path = NULL;
    const char *key_str = NULL;

    for(int i = 1; i < argc; i++)
    {
        if(strcmp(argv[i], "-b") == 0 && i + 1 < argc)
        {
            db_path = argv[++i];
        }
        else if(strcmp(argv[i], "-k") == 0 && i + 1 < argc)
        {
            key_str = argv[++i];
        }
    }

    if(db_path == NULL || key_str == NULL)
    {
        fprintf(stderr, "Usage: %s -b <db_path> -k <key>\n", argv[0]);
        return EXIT_FAILURE;
    }

    DBM *db = dbm_open((char *)db_path, O_RDONLY, 0);
    if(db == NULL)
    {
        fprintf(stderr, "Error: could not open database '%s': %s\n", db_path, strerror(errno));
        return EXIT_FAILURE;
    }

    datum key;
    key.dptr  = (char *)key_str;
    key.dsize = (int)strlen(key_str) + 1;

    datum val = dbm_fetch(db, key);
    if(val.dptr == NULL)
    {
        fprintf(stderr, "Key '%s' not found in database.\n", key_str);
        dbm_close(db);
        return EXIT_FAILURE;
    }

    printf("%s\n", (char *)val.dptr);
    dbm_close(db);
    return EXIT_SUCCESS;
}