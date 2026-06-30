#ifndef CB_VALIDATE_H
#define CB_VALIDATE_H

#include <stddef.h>

/* Validation return codes */
#define VALIDATE_OK  0
#define VALIDATE_ERR -1

/* Returns VALIDATE_OK if name is valid, VALIDATE_ERR otherwise.
 * error_out (optional) receives a descriptive error message. */
int validate_repo_name(const char *name, char *error_out, size_t error_sz);

/* Returns VALIDATE_OK if description length is within limits. */
int validate_description(const char *desc, char *error_out, size_t error_sz);

/* Returns VALIDATE_OK if website URL length is within limits. */
int validate_website(const char *url, char *error_out, size_t error_sz);

/* Returns VALIDATE_OK if merge style is a valid enum value. */
int validate_merge_style(const char *style, char *error_out, size_t error_sz);

/* Parse "owner/repo" or "repo" (owner defaults to empty string).
 * owner_out and repo_out are caller-provided buffers.
 * Returns VALIDATE_OK on success, VALIDATE_ERR if format is invalid.
 * If no slash is present, owner_out is set to empty string and repo_out gets the full input.
 * If slash is present but owner or repo is empty, returns VALIDATE_ERR.
 */
int validate_owner_repo(const char *str, char *owner_out, size_t owner_sz,
                        char *repo_out, size_t repo_sz, char *error_out, size_t error_sz);

#endif /* CB_VALIDATE_H */
