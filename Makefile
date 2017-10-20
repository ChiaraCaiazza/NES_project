CONTIKI_PROJECT =  CentralUnit Node1 Node2 extension_node

all: $(CONTIKI_PROJECT)

CONTIKI = /home/user/contiki

CONTIKI_WITH_RIME = 1

MODULES += dev/sht11

TARGET_LIBFILES += -lm

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

include $(CONTIKI)/Makefile.include
