#include <stdio.h>
#include "ccl.h"

void usage(const char *prog_name)
{
  printf("usage: %s config_file\n", prog_name);
}

int main(int argc,char **argv)
{
  struct ccl_t 			config;
  const struct ccl_pair 	*iter;

  if(argc == 1) {
    usage(argv[0]);
    return 0;
  }

  config.comment_char = '#';
  config.sep_char = '=';
  config.str_char = '"';

  ccl_parse(&config, argv[1]);

  while((iter = ccl_iterator(&config)) != 0) {
    printf("(%s,%s)\n", iter->key, iter->value);
  }
  ccl_release(&config);

  return 0;
}
