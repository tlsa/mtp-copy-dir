
/*
 * Recursive copy from path to MTP device root.
 *
 * By Michael Drake, 2013
 *
 * Build with:
 *   gcc -o mtp-copy-dir mtp-copy-dir.c `pkg-config --cflags --libs libmtp`
 */

#include <dirent.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <libmtp.h>

bool mtp_copy(LIBMTP_mtpdevice_t *device, uint32_t parent_id, const char *path);


bool mtp_copy_file(LIBMTP_mtpdevice_t *device, uint32_t parent_id,
		const char *path, struct stat *s)
{
	char *filename;
	char *path2;
	uint64_t filesize;
	LIBMTP_file_t *genfile;
	int ret;
	bool ok = true;

	path2 = strdup(path);

	filesize = s->st_size;
	filename = basename(path2);

	genfile = LIBMTP_new_file_t();
	genfile->filesize = filesize;
	genfile->filename = strdup(filename);
	/* TODO: will need to improve if other filetypes wanted */
	genfile->filetype = LIBMTP_FILETYPE_MP3;
	genfile->parent_id = parent_id;
	genfile->storage_id = 0;

	/* Copy to device */
	ret = LIBMTP_Send_File_From_File(device, path, genfile, NULL, NULL);
	if (ret != 0) {
		printf("Error sending \"%s\"\n", path);
		ok = false;
	} else {
		printf("Copied %s\n", filename);
	}

	free(path2);
	LIBMTP_destroy_file_t(genfile);

	return ok;
}

bool mtp_copy_dir(LIBMTP_mtpdevice_t *device, uint32_t parent_id,
		const char *path)
{
	char *filename;
	struct dirent **listing;
	uint32_t dir_id;
	size_t new_path_len = 0;
	size_t path_len = strlen(path);
	size_t name_len;
	char *new_path = NULL;
	char *path2;
	int i, n;

	path2 = strdup(path);
	if (path2 == NULL) {
		return false;
	}
	filename = basename(path2);

	/* Make the directory on device */
	dir_id = LIBMTP_Create_Folder(device, filename, parent_id, 0);
	if (dir_id == 0) {
		printf("Failed copying folder:\n\t%s\n", path);
		free(path2);
		return false;
	}
	free(path2);

	printf("Copying %s\n", path);

	/* Deal with the directory's contents */
	n = scandir(path, &listing, 0, alphasort);
	if (n < 0) {
		free(listing);
		return false;
	}

	for (i = 0; i < n; i++) {
		char *temp;
		if (listing[i]->d_name[0] == '.')
			continue;

		name_len = strlen(listing[i]->d_name);

		if (new_path_len < path_len + 1 + name_len + 1) {
			temp = realloc(new_path, path_len + 1 + name_len + 1);
			if (temp == NULL) {
				break;
			}
			new_path = temp;
			new_path_len = path_len + 1 + name_len + 1;
		}

		sprintf(new_path, "%s/%s", path, listing[i]->d_name);

		mtp_copy(device, dir_id, new_path);
	}

	/* Free the directory listing */
	for (i = 0; i < n; i++)
		free(listing[i]);
	free(listing);

	free(new_path);
	return true;
}

bool mtp_copy(LIBMTP_mtpdevice_t *device, uint32_t parent_id,
		const char *path)
{
	struct stat s;

	if (stat(path, &s) != 0) {
		printf("Couldn't stat %s\n", path);
		return false;
	}

	if (S_ISDIR(s.st_mode)) {
		if (!mtp_copy_dir(device, parent_id, path))
			return false;
	} else if (S_ISREG(s.st_mode)) {
		if (!mtp_copy_file(device, parent_id, path, &s))
			return false;
	} else {
		return false;
	}

	return true;
}

int main(int argc, char *argv[])
{
	LIBMTP_mtpdevice_t *device_list, *device;
	char *path;
	char *name;
	int d;

	/* Deal with command line arguments */
	if (argc != 2) {
		printf("Usage: %s [path]\n", argv[0]);
		return EXIT_FAILURE;
	}
	path = realpath(argv[1], NULL);
	if (path == NULL) {
		return EXIT_FAILURE;
	}

	LIBMTP_Init();

	/* Find MTP devices */
	switch (LIBMTP_Get_Connected_Devices(&device_list)) {
	case LIBMTP_ERROR_NO_DEVICE_ATTACHED:
		printf("No devices found.\n");
		free(path);
		return EXIT_SUCCESS;
	case LIBMTP_ERROR_CONNECTING:
		printf("Connection error.\n");
		free(path);
		return EXIT_FAILURE;
	case LIBMTP_ERROR_MEMORY_ALLOCATION:
		printf("Out of memory.\n");
		free(path);
		return EXIT_FAILURE;
	case LIBMTP_ERROR_GENERAL:
	default:
		printf("Unknown error.\n");
		free(path);
		return EXIT_FAILURE;
	case LIBMTP_ERROR_NONE:
		/* Connected to a device, so continue. */
		break;
	}

	if (device_list == NULL) {
		printf("Unknown error.\n");
		free(path);
		return EXIT_FAILURE;
	}

	/* List MTP devices */
	printf("\nConnected MTP devices:\n");
	d = 0;
	for (device = device_list; device != NULL; device = device->next) {
		name = LIBMTP_Get_Friendlyname(device);
		printf("\t%*d: %s\n", 3, d++, name);
	}

	/* Allow user selection of device */
	/* TODO: for now, just use first device */
	d = 0;

	/* Get device */
	device = device_list;
	while (device->next != NULL && d > 0) {
		device = device->next;
		d--;
	}

	/* Indicate which MTP device we're copying to */
	name = LIBMTP_Get_Friendlyname(device);
	printf("Copying to %s\n", name);

	if (!mtp_copy(device, 0, path)) {
		free(path);
		return EXIT_FAILURE;
	}

	free(path);
	return EXIT_SUCCESS;
}
