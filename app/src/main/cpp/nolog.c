/* NDK glue logs through __android_log_print. Resolve it locally to drop liblog. */
int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
    (void)prio;
    (void)tag;
    (void)fmt;
    return 0;
}
