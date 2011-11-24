#makfile for making nrxtest for a PC
include Android.mk

nrxtest: $(LOCAL_SRC_FILES)
	gcc -o $(LOCAL_MODULE) $(LOCAL_SRC_FILES) -I $(LOCAL_C_INCLUDES)

clean:
	rm nrxtest