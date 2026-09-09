#include "types.h"

void*
memset(void *dst, int c, uint n)
{
  char *cdst = (char *) dst;
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  return dst;
}

int
memcmp(const void *v1, const void *v2, uint n)
{
  const uchar *s1, *s2;

  s1 = v1;
  s2 = v2;
  while(n-- > 0){
    if(*s1 != *s2)
      return *s1 - *s2;
    s1++, s2++;
  }

  return 0;
}

void*
memmove(void *dst, const void *src, uint n)
{
  const char *s;
  char *d;

  if(n == 0)
    return dst;
  
  s = src;
  d = dst;
  if(s < d && s + n > d){
    s += n;
    d += n;
    while(n-- > 0)
      *--d = *--s;
  } else
    while(n-- > 0)
      *d++ = *s++;

  return dst;
}

// memcpy exists to placate GCC.  Use memmove.
void*
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

int
strncmp(const char *p, const char *q, uint n)
{
  while(n > 0 && *p && *p == *q)
    n--, p++, q++;
  if(n == 0)
    return 0;
  return (uchar)*p - (uchar)*q;
}

char*
strncpy(char *s, const char *t, int n)
{
  char *os;

  os = s;
  while(n-- > 0 && (*s++ = *t++) != 0)
    ;
  while(n-- > 0)
    *s++ = 0;
  return os;
}

// Like strncpy but guaranteed to NUL-terminate.
char*
safestrcpy(char *s, const char *t, int n)
{
  char *os;

  os = s;
  if(n <= 0)
    return os;
  while(--n > 0 && (*s++ = *t++) != 0)
    ;
  *s = 0;
  return os;
}

int
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void reverse(char str[], int length)
{
  int start = 0;
  int end = length - 1;
  while (start < end)
  {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
    start++;
    end--;
  }
}

char* itoa(int num, char* str)
{
  int i = 0;
  int isNegative = 0;

  // Handle 0 explicitly, otherwise empty string is printed
  if (num == 0)
  {
    str[i++] = '0';
    str[i] = '\0';
    return str;
  }
  if (num < 0)
  {
    isNegative = 1;
    num = -num; // Make number positive
  }

  // Process individual digits
  while (num != 0)
  {
    int rem = num % 10;
    str[i++] = (rem > 9) ? (rem - 10 + 'a') : (rem + '0');
    num = num / 10;
  }

  // Append minus sign if negative
  if (isNegative)
  {
    str[i++] = '-';
  }

  str[i] = '\0'; // Null-terminate string

  // Reverse the string as digits were appended in reverse order
  reverse(str, i);

  return str;
}

void concat(char* dest, const char* s1, const char* s2)
{
  int i = 0, j = 0;

  while (s1[i])
  {
    dest[i] = s1[i];
    i++;
  }

  while (s2[j])
  {
    dest[i + j] = s2[j];
    j++;
  }

  dest[i + j] = '\0';
}