#include "common.h"
#include <stddef.h>
#include <string.h>

const char *getQueryValue(char *url, const char *key, char *value, size_t n) {
  char *temp = strchr(url, '?');
  if (temp == NULL)
    return NULL;
  temp++;
  temp = strstr(temp, key);
  if (temp == NULL)
    return NULL;
  temp = temp + strlen(key);
  if (*temp != '=')
    return NULL;
  char *fn_queryValue = temp + 1;
  temp = strchr(fn_queryValue, '&');
  size_t len = strlen(fn_queryValue);
  if (temp)
    len = (size_t)temp - (size_t)fn_queryValue;

  if (len > n - 1)
    len = n - 1;
  memcpy(value, fn_queryValue, len);
  fn_queryValue[len] = '\0';
  return fn_queryValue;
}
