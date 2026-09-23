void collapseName(void);
void collapseSafety(void);
void collapseUnavail(void);
void collapsePrivate(void);
id collapseRetain(void) __attribute__((ns_returns_retained));
void collapseParam(int *p);
void collapseBadName(void) __attribute__((swift_name("header()")));
void collapseBadAlias(void);
void collapseBadBase(void);
void collapseUbu(void);
void collapseSafetyOther(void) __attribute__((swift_attr("unrelated")))
__attribute__((swift_attr("safe")));
void collapseAvailPlatform(void)
    __attribute__((availability(macos, introduced = 10.10)));
struct collapseOrder {
  int x;
};
__attribute__((objc_root_class))
@interface CollapseBox
- (void)collapseMethod:(int *)p;
@end
