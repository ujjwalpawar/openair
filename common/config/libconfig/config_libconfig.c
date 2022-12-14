/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */
/*! \file common/config/libconfig/config_libconfig.c
 * \brief: implementation libconfig configuration library
 * \author Francois TABURET
 * \date 2017
 * \version 0.1
 * \company NOKIA BellLabs France
 * \email: francois.taburet@nokia-bell-labs.com
 * \note
 * \warning
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <libconfig.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
 #include <libgen.h>


#include "config_libconfig.h"
#include "config_libconfig_private.h"
#include "../config_userapi.h"
#include "errno.h"
#include "assertions.h"

#if ( LIBCONFIG_VER_MAJOR == 1 && LIBCONFIG_VER_MINOR < 5)
  #define config_setting_lookup config_lookup_from
#endif

void config_libconfig_end(void );

int read_strlist(paramdef_t *cfgoptions,config_setting_t *setting, char *cfgpath) {
  const char *str;
  int st;
  int numelt;
  numelt=config_setting_length(setting);
  config_check_valptr(cfgoptions, sizeof(char *), numelt);
  st=0;
  AssertFatal(MAX_LIST_SIZE > numelt, 
              "This piece of code use fixed size arry of constant #define MAX_LIST_SIZE %d\n", 
              MAX_LIST_SIZE );
  for (int i=0; i< numelt ; i++) {
    str=config_setting_get_string_elem(setting,i);

    if (str==NULL) {
      printf("[LIBCONFIG] %s%i  not found in config file\n", cfgoptions->optname,i);
    } else {
      snprintf(cfgoptions->strlistptr[i], DEFAULT_EXTRA_SZ, "%s",str);
      st++;
      printf_params("[LIBCONFIG] %s%i: %s\n", cfgpath,i,cfgoptions->strlistptr[i]);
    }
  }

  cfgoptions->numelt=numelt;
  return st;
}

int read_intarray(paramdef_t *cfgoptions,config_setting_t *setting, char *cfgpath) {
  cfgoptions->numelt=config_setting_length(setting);

  if (cfgoptions->numelt > 0) {
    cfgoptions->iptr=config_allocate_new(cfgoptions->numelt*sizeof(*cfgoptions->iptr),
                                         !(cfgoptions->paramflags & PARAMFLAG_NOFREE) );

    for (int i=0; i< cfgoptions->numelt && cfgoptions->iptr != NULL; i++) {
      cfgoptions->iptr[i]=config_setting_get_int_elem(setting,i);
      printf_params("[LIBCONFIG] %s[%i]: %i\n", cfgpath,i,cfgoptions->iptr[i]);
    } /* for loop on array element... */
  }

  return cfgoptions->numelt;
}




int config_libconfig_get(paramdef_t *cfgoptions,int numoptions, char *prefix ) {
  config_setting_t *setting;
  char *str;
  int i,u;
  long long int llu;
  double dbl;
  int rst;
  int status=0;
  int notfound;
  int defval;
  int fatalerror=0;
  int numdefvals=0;
  char cfgpath[512];  /* 512 should be enough for the sprintf below */

  for(i=0; i<numoptions; i++) {
    if (prefix != NULL) {
      sprintf(cfgpath,"%s.%s",prefix,cfgoptions[i].optname);
    } else {
      sprintf(cfgpath,"%s",cfgoptions[i].optname);
    }

    if( (cfgoptions->paramflags & PARAMFLAG_DONOTREAD) != 0) {
      printf_params("[LIBCONFIG] %s.%s ignored\n", cfgpath,cfgoptions[i].optname );
      continue;
    }

    notfound=0;
    defval=0;

    switch(cfgoptions[i].type) {
      case TYPE_STRING:
        if ( config_lookup_string(&(libconfig_privdata.cfg), cfgpath, (const char**)&str)) {
          config_check_valptr(&(cfgoptions[i]), 1, strlen(str)+1);
          if ( strlen(str)+1 > cfgoptions[i].numelt )
            fprintf(stderr,"[LIBCONFIG] %s:  %s exceeds maximum length of %i bytes, value truncated\n",
                    cfgpath,str,cfgoptions[i].numelt);
          snprintf( *cfgoptions[i].strptr , cfgoptions[i].numelt, "%s", str);
          printf_params("[LIBCONFIG] %s: \"%s\"\n", cfgpath, *cfgoptions[i].strptr);
        } else {
          defval=config_setdefault_string(&cfgoptions[i],prefix);
        }

        break;

      case TYPE_STRINGLIST:
        setting = config_setting_lookup (config_root_setting(&(libconfig_privdata.cfg)),cfgpath );

        if ( setting != NULL) {
          read_strlist(&cfgoptions[i],setting,cfgpath);
        } else {
          defval=config_setdefault_stringlist(&(cfgoptions[i]),prefix);
        }

        break;

      case TYPE_UINT8:
      case TYPE_INT8:
      case TYPE_UINT16:
      case TYPE_INT16:
      case TYPE_UINT32:
      case TYPE_INT32:
      case TYPE_MASK:
        if ( config_lookup_int(&(libconfig_privdata.cfg),cfgpath, &u)) {
          config_check_valptr(&(cfgoptions[i]), sizeof(*cfgoptions[i].iptr), 1);
          config_assign_int(&(cfgoptions[i]),cfgpath,u);
        } else {
          defval=config_setdefault_int(&(cfgoptions[i]),prefix);
        }

        break;

      case TYPE_UINT64:
      case TYPE_INT64:
        if ( config_lookup_int64(&(libconfig_privdata.cfg),cfgpath, &llu)) {
          config_check_valptr(&cfgoptions[i], sizeof(*cfgoptions[i].i64ptr), 1);

          if(cfgoptions[i].type==TYPE_UINT64) {
            *(cfgoptions[i].u64ptr) = (uint64_t)llu;
            printf_params("[LIBCONFIG] %s: %lu\n", cfgpath,*cfgoptions[i].u64ptr);
          } else {
            *(cfgoptions[i].i64ptr) = llu;
            printf_params("[LIBCONFIG] %s: %ld\n", cfgpath,*cfgoptions[i].i64ptr);
          }
        } else {
          defval=config_setdefault_int64(&(cfgoptions[i]),prefix);
        }

        break;

      case TYPE_UINTARRAY:
      case TYPE_INTARRAY:
        setting = config_setting_lookup (config_root_setting(&(libconfig_privdata.cfg)),cfgpath );

        if ( setting != NULL) {
          read_intarray(&cfgoptions[i],setting,cfgpath);
        } else {
          defval=config_setdefault_intlist(&(cfgoptions[i]),prefix);
        }

        break;

      case TYPE_DOUBLE:
        if ( config_lookup_float(&(libconfig_privdata.cfg),cfgpath, &dbl)) {
          config_check_valptr(&cfgoptions[i],sizeof(*cfgoptions[i].dblptr), 1);
          *cfgoptions[i].dblptr = dbl;
          printf_params("[LIBCONFIG] %s: %lf\n", cfgpath,*(cfgoptions[i].dblptr) );
        } else {
          defval=config_setdefault_double(&(cfgoptions[i]),prefix);
        }

        break;

      case TYPE_IPV4ADDR:
        if ( !config_lookup_string(&(libconfig_privdata.cfg),cfgpath, (const char **)&str)) {
          defval=config_setdefault_ipv4addr(&(cfgoptions[i]),prefix);
        } else {
          rst=config_assign_ipv4addr(cfgoptions, str);

          if (rst < 0) {
            fprintf(stderr,"[LIBCONFIG] %s not valid for %s \n", str, cfgpath);
            fatalerror=1;
          }
        }

        break;

      case TYPE_LIST:
        setting = config_setting_lookup (config_root_setting(&(libconfig_privdata.cfg)),cfgpath );

        if ( setting) {
          cfgoptions[i].numelt=config_setting_length(setting);
        } else {
          notfound=1;
        }

        break;

      default:
        fprintf(stderr,"[LIBCONFIG] %s type %i  not supported\n", cfgpath,cfgoptions[i].type);
        fatalerror=1;
        break;
    } /* switch on param type */

    if( notfound == 1) {
      printf("[LIBCONFIG] %s not found in %s ", cfgpath,libconfig_privdata.configfile );

      if ( (cfgoptions[i].paramflags & PARAMFLAG_MANDATORY) != 0) {
        fatalerror=1;
        printf("  mandatory parameter missing\n");
      } else {
        printf("\n");
      }
    } else {
      if (defval == 1) {
        numdefvals++;
        cfgoptions[i].paramflags = cfgoptions[i].paramflags |  PARAMFLAG_PARAMSETDEF;
      } else {
        cfgoptions[i].paramflags = cfgoptions[i].paramflags |  PARAMFLAG_PARAMSET;
      }

      status++;
    }
  } /* for loop on options */

  printf("[LIBCONFIG] %s: %i/%i parameters successfully set, (%i to default value)\n",
         ((prefix == NULL)?"(root)":prefix),
         status,numoptions,numdefvals );

  if (fatalerror == 1) {
    fprintf(stderr,"[LIBCONFIG] fatal errors found when processing  %s \n", libconfig_privdata.configfile );
    config_libconfig_end();
    end_configmodule();
  }

  return status;
}

int config_libconfig_getlist(paramlist_def_t *ParamList,
                             paramdef_t *params, int numparams, char *prefix) {
  config_setting_t *setting;
  int i,j,status;
  char *listpath=NULL;
  char cfgpath[MAX_OPTNAME_SIZE*2 + 6]; /* prefix.listname.[listindex] */

  if (prefix != NULL) {
    i=asprintf(&listpath ,"%s.%s",prefix,ParamList->listname);
  } else {
    i=asprintf(&listpath ,"%s",ParamList->listname);
  }

  setting = config_lookup(&(libconfig_privdata.cfg), listpath);

  if ( setting) {
    status = ParamList->numelt = config_setting_length(setting);
    printf_params("[LIBCONFIG] %i %s in config file %s \n",
                  ParamList->numelt,listpath,libconfig_privdata.configfile );
  } else {
    printf("[LIBCONFIG] list %s not found in config file %s \n",
           listpath,libconfig_privdata.configfile );
    ParamList->numelt= 0;
    status = -1;
  }

  if (ParamList->numelt > 0 && params != NULL) {
    ParamList->paramarray = config_allocate_new(ParamList->numelt * sizeof(paramdef_t *), true);

    for (i=0 ; i < ParamList->numelt ; i++) {
      ParamList->paramarray[i] = config_allocate_new(numparams * sizeof(paramdef_t), true);
      memcpy(ParamList->paramarray[i], params, sizeof(paramdef_t)*numparams);

      for (j=0; j<numparams; j++) {
        ParamList->paramarray[i][j].strptr = NULL ;
      }

      sprintf(cfgpath,"%s.[%i]",listpath,i);
      config_libconfig_get(ParamList->paramarray[i], numparams, cfgpath );
    } /* for i... */
  } /* ParamList->numelt > 0 && params != NULL */

  if (listpath != NULL)
    free(listpath);

  return status;
}

int config_libconfig_init(char *cfgP[], int numP) {
  config_init(&(libconfig_privdata.cfg));
  libconfig_privdata.configfile = strdup((char *)cfgP[0]);
  config_get_if()->numptrs=0;
  pthread_mutex_init(&config_get_if()->memBlocks_mutex, NULL);
  memset(config_get_if()->oneBlock,0,sizeof(config_get_if()->oneBlock));
  /* search for include path parameter and set config file include path accordingly */
  for (int i=0; i<numP; i++) {
  	  if (strncmp(cfgP[i],"incp",4) == 0) {
  	  	  config_set_include_dir (&(libconfig_privdata.cfg),cfgP[i]+4);
  	  break;
  	  }
  }
  /* dirname may modify the input path and returned ptr may points to part of input path */
  char *tmppath = strdup(libconfig_privdata.configfile);
  if ( config_get_include_dir (&(libconfig_privdata.cfg)) == NULL) {
  	 config_set_include_dir (&(libconfig_privdata.cfg),dirname( tmppath )); 	 
  }
  
  const char *incp = config_get_include_dir (&(libconfig_privdata.cfg)) ;
 
  printf("[LIBCONFIG] Path for include directive set to: %s\n", (incp!=NULL)?incp:"libconfig defaults");
  /* set convertion option to allow integer to float conversion*/
   config_set_auto_convert (&(libconfig_privdata.cfg), CONFIG_TRUE);
  /* Read the file. If there is an error, report it and exit. */
  if( config_read_file(&(libconfig_privdata.cfg), libconfig_privdata.configfile) == CONFIG_FALSE) {
    fprintf(stderr,"[LIBCONFIG] %s %d file %s - line %d: %s\n",__FILE__, __LINE__,
            libconfig_privdata.configfile, config_error_line(&(libconfig_privdata.cfg)),
            config_error_text(&(libconfig_privdata.cfg)));
    config_destroy(&(libconfig_privdata.cfg));
    printf( "\n");
    free(tmppath);
    return -1;
  }
  free(tmppath);
  return 0;
}


void config_libconfig_end(void ) {
  config_destroy(&(libconfig_privdata.cfg));

  if ( libconfig_privdata.configfile != NULL ) {
    free(libconfig_privdata.configfile);
    libconfig_privdata.configfile=NULL;
  }
}
