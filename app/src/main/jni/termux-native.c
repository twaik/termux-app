#include <dlfcn.h>
#include <android/native_activity.h>
#include <android/log.h>
#include <android-dl.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TermuxNative", __VA_ARGS__)
#define LIBLORIE "/data/data/com.termux/files/usr/lib/liblorie-android.so"

typedef void (*SENDINPUTEVENTF)(JNIEnv*, jobject, jlong, jobject);

void finish(ANativeActivity* activity) {
    JNIEnv *env = activity->env;
    jclass cls = NULL;
    jmethodID _finish = NULL;

    cls = (*env)->GetObjectClass(env, activity->clazz);
    if (cls != NULL) _finish = (*env)->GetMethodID(env, cls, "finish", "()V");
    if (_finish != NULL) (*env)->CallVoidMethod(env, activity->clazz, _finish);
}

static void *loadSym(const char *lib, const char *name) {
    void *libHandle = NULL;
    void *func = NULL;
    char *error = NULL;
    libHandle = android_dlopen(lib);
    if (!libHandle) {
        LOGE("Unable to load %s: %s", lib, (error=dlerror())?error:"unknown error");
        return NULL;
    }

    func = android_dlsym(libHandle, name);
    if (func == NULL)  {
        LOGE("Unable to find symbol %s: %s", name, (error=dlerror())?error:"unknown error");
        return NULL;
    }
    return func;
}

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved) {
	jint (*onLoad)(JavaVM*, void*) = loadSym(LIBLORIE, "JNI_OnLoad");
	if (onLoad != NULL) return onLoad(vm, reserved);
	return -1;
}

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *reserved) {
	jint (*onUnload)(JavaVM*, void*) = loadSym(LIBLORIE, "JNI_OnUnload");
	if (onUnload != NULL) onUnload(vm, reserved);
}
