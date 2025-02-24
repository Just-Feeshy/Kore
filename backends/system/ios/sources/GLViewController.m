#import "GLView.h"
#import "GLViewController.h"

#import <Foundation/Foundation.h>

#include <kore3/gpu/texture.h>
#include <kore3/math/core.h>

#include <objc/runtime.h>

static GLView *glView;

static bool visible;

void beginGL(void) {
#ifdef KORE_METAL
	if (!visible) {
		return;
	}
#endif
	[glView begin];
}

void endGL(void) {
#ifdef KORE_METAL
	if (!visible) {
		return;
	}
#endif
	[glView end];
}

void showKeyboard(void) {
	[glView showKeyboard];
}

void hideKeyboard(void) {
	[glView hideKeyboard];
}

#ifdef KORE_METAL

CAMetalLayer *getMetalLayer(void) {
	return [glView metalLayer];
}

id getMetalDevice(void) {
	return [glView metalDevice];
}

id getMetalLibrary(void) {
	return [glView metalLibrary];
}

id getMetalQueue(void) {
	return [glView metalQueue];
}

#endif

@implementation GLViewController

- (void)loadView {
	visible = true;
	self.view = glView = [[GLView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
}

- (void)setVisible:(BOOL)value {
	visible = value;
}

@end
