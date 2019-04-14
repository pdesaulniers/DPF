/*
 * DISTRHO Plugin Framework (DPF)
 * Copyright (C) 2012-2018 Filipe Coelho <falktx@falktx.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

// we need this for now
//#define PUGL_GRAB_FOCUS 1

#include "../Base.hpp"

#undef PUGL_HAVE_CAIRO
#undef PUGL_HAVE_GL
#define PUGL_HAVE_GL 1

#include "pugl/pugl.h"

#if defined(__GNUC__) && (__GNUC__ >= 7)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

#if defined(DISTRHO_OS_WINDOWS)
#include "pugl/pugl_win.cpp"
#elif defined(DISTRHO_OS_MAC)
#include "pugl/pugl_osx.m"
#else
#include <sys/types.h>
#include <unistd.h>
#include <X11/cursorfont.h>

extern "C" {
#include "pugl/pugl_x11.c"
}
#endif

#if defined(__GNUC__) && (__GNUC__ >= 7)
#pragma GCC diagnostic pop
#endif

#include "ApplicationPrivateData.hpp"
#include "WidgetPrivateData.hpp"
#include "../StandaloneWindow.hpp"
#include "../../distrho/extra/String.hpp"

#define FOR_EACH_WIDGET(it) \
	for (std::list<Widget *>::iterator it = fWidgets.begin(); it != fWidgets.end(); ++it)

#define FOR_EACH_WIDGET_INV(rit) \
	for (std::list<Widget *>::reverse_iterator rit = fWidgets.rbegin(); rit != fWidgets.rend(); ++rit)

#ifdef DEBUG
#define DBG(msg) std::fprintf(stderr, "%s", msg);
#define DBGp(...) std::fprintf(stderr, __VA_ARGS__);
#define DBGF std::fflush(stderr);
#else
#define DBG(msg)
#define DBGp(...)
#define DBGF
#endif

START_NAMESPACE_DGL

// -----------------------------------------------------------------------
// Window Private

struct Window::PrivateData
{
	PrivateData(Application &app, Window *const self)
		: fApp(app),
		  fSelf(self),
		  fView(puglInit(NULL, NULL)),
		  fFirstInit(true),
		  fVisible(false),
		  fResizable(true),
		  fUsingEmbed(false),
		  fWidth(1),
		  fHeight(1),
		  fTitle(nullptr),
		  fWidgets(),
		  fModal(),
#if defined(DISTRHO_OS_WINDOWS)
		  hwnd(0)
#elif defined(DISTRHO_OS_MAC)
		  fNeedsIdle(true),
		  mView(nullptr),
		  mWindow(nullptr)
#else
		  xDisplay(nullptr),
		  xWindow(0),
		  xClipCursorWindow(0)
#endif
	{
		DBG("Creating window without parent...");
		DBGF;
		init();
	}

	PrivateData(Application &app, Window *const self, Window &parent)
		: fApp(app),
		  fSelf(self),
		  fView(puglInit(NULL, NULL)),
		  fFirstInit(true),
		  fVisible(false),
		  fResizable(true),
		  fUsingEmbed(false),
		  fWidth(1),
		  fHeight(1),
		  fTitle(nullptr),
		  fWidgets(),
		  fModal(parent.pData),
#if defined(DISTRHO_OS_WINDOWS)
		  hwnd(0)
#elif defined(DISTRHO_OS_MAC)
		  fNeedsIdle(false),
		  mView(nullptr),
		  mWindow(nullptr)
#else
		  xDisplay(nullptr),
		  xWindow(0),
		  xClipCursorWindow(0)
#endif
	{
		DBG("Creating window with parent...");
		DBGF;
		init();

		const PuglInternals *const parentImpl(parent.pData->fView->impl);
#if defined(DISTRHO_OS_WINDOWS)
		// TODO
#elif defined(DISTRHO_OS_MAC)
		[parentImpl->window orderWindow:NSWindowBelow
							 relativeTo:[[mView window] windowNumber]];
#else
		XSetTransientForHint(xDisplay, xWindow, parentImpl->win);
#endif
		return;

		// maybe unused
		(void)parentImpl;
	}

	PrivateData(Application &app, Window *const self, const intptr_t parentId)
		: fApp(app),
		  fSelf(self),
		  fView(puglInit(NULL, NULL)),
		  fFirstInit(true),
		  fVisible(parentId != 0),
		  fResizable(parentId == 0),
		  fUsingEmbed(parentId != 0),
		  fWidth(1),
		  fHeight(1),
		  fTitle(nullptr),
		  fWidgets(),
		  fModal(),
		  fCursorIsClipped(false),
		  fIsFullscreen(false),
		  fPreFullscreenSize(Size<uint>(0,0)),
		  fIsContextMenu(false),
#if defined(DISTRHO_OS_WINDOWS)
		  hwnd(0)
#elif defined(DISTRHO_OS_MAC)
		  fNeedsIdle(parentId == 0),
		  mView(nullptr),
		  mWindow(nullptr)
#else
		  xDisplay(nullptr),
		  xWindow(0),
		  xClipCursorWindow(0)
#endif
	{
		if (fUsingEmbed)
		{
			DBG("Creating embedded window...");
			DBGF;
			puglInitWindowParent(fView, parentId);
		}
		else
		{
			DBG("Creating window without parent...");
			DBGF;
		}

		init();

		if (fUsingEmbed)
		{
			DBG("NOTE: Embed window is always visible and non-resizable\n");
			puglShowWindow(fView);
			fApp.pData->oneShown();
			fFirstInit = false;
		}
	}

	void init()
	{
		if (fSelf == nullptr || fView == nullptr)
		{
			DBG("Failed!\n");
			return;
		}

		puglInitContextType(fView, PUGL_GL);
		puglInitResizable(fView, fResizable);
		puglInitWindowSize(fView, static_cast<int>(fWidth), static_cast<int>(fHeight));
		puglSetHandle(fView, this);
		puglSetEventFunc(fView, onEventCallback);

#ifndef DGL_FILE_BROWSER_DISABLED
		puglSetFileSelectedFunc(fView, fileBrowserSelectedCallback);
#endif

		puglCreateWindow(fView, nullptr);

		PuglInternals *impl = fView->impl;
#if defined(DISTRHO_OS_WINDOWS)
		hwnd = impl->hwnd;
		DISTRHO_SAFE_ASSERT(hwnd != 0);
#elif defined(DISTRHO_OS_MAC)
		mView = impl->glview;
		mWindow = impl->window;
		DISTRHO_SAFE_ASSERT(mView != nullptr);
		if (fUsingEmbed)
		{
			DISTRHO_SAFE_ASSERT(mWindow == nullptr);
		}
		else
		{
			DISTRHO_SAFE_ASSERT(mWindow != nullptr);
		}
#else
		xDisplay = impl->display;
		xWindow = impl->win;
		DISTRHO_SAFE_ASSERT(xWindow != 0);

		if (!fUsingEmbed)
		{
			pid_t pid = getpid();
			Atom _nwp = XInternAtom(xDisplay, "_NET_WM_PID", True);
			XChangeProperty(xDisplay, xWindow, _nwp, XA_CARDINAL, 32, PropModeReplace, (const uchar *)&pid, 1);
		}

		//fork---------
		//init invisible cursor, should probably be done elsewhere
		XColor black;
		black.red = black.green = black.blue = 0;

		const char noData[] = {0, 0, 0, 0, 0, 0, 0, 0};

		Pixmap bitmapNoData = XCreateBitmapFromData(xDisplay, xWindow, noData, 8, 8);
		invisibleCursor = XCreatePixmapCursor(xDisplay, bitmapNoData, bitmapNoData, &black, &black, 0, 0);

		XFreePixmap(xDisplay, bitmapNoData);

		xClipCursorWindow = XCreateWindow(xDisplay, xWindow, 0, 0, fWidth, fHeight, 0, 0, InputOnly, NULL, 0, NULL);

		XMapWindow(xDisplay, xClipCursorWindow);
		//-------------
#endif
		fMustSaveSize = false;

		puglEnterContext(fView);

		fApp.pData->windows.push_back(fSelf);

		DBG("Success!\n");
	}

	~PrivateData()
	{
		DBG("Destroying window...");
		DBGF;

		if (fModal.enabled)
		{
			exec_fini();
			close();
		}

		fWidgets.clear();

		if (fUsingEmbed)
		{
			puglHideWindow(fView);
			fApp.pData->oneHidden();
		}

		if (fSelf != nullptr)
		{
			fApp.pData->windows.remove(fSelf);
			fSelf = nullptr;
		}

		if (fView != nullptr)
		{
			puglDestroy(fView);
			fView = nullptr;
		}

		if (fTitle != nullptr)
		{
			std::free(fTitle);
			fTitle = nullptr;
		}

#if defined(DISTRHO_OS_WINDOWS)
		hwnd = 0;
#elif defined(DISTRHO_OS_MAC)
		mView = nullptr;
		mWindow = nullptr;
#else
		xDisplay = nullptr;
		xWindow = 0;
		xClipCursorWindow = 0;
#endif

		DBG("Success!\n");
	}

	// -------------------------------------------------------------------

	void close()
	{
		DBG("Window close\n");
		fSelf->onClose();
		
		if (fUsingEmbed)
			return;

		setVisible(false);

		if (!fFirstInit)
		{
			fApp.pData->oneHidden();
			fFirstInit = true;
		}
	}

	void exec(const bool lockWait)
	{
		DBG("Window exec\n");
		exec_init();

		if (lockWait)
		{
			for (; fVisible && fModal.enabled;)
			{
				idle();
				d_msleep(10);
			}

			exec_fini();
		}
		else
		{
			idle();
		}
	}

	// -------------------------------------------------------------------

	void exec_init()
	{
		DBG("Window modal loop starting...");
		DBGF;
		DISTRHO_SAFE_ASSERT_RETURN(fModal.parent != nullptr, setVisible(true));

		fModal.enabled = true;
		fModal.parent->fModal.childFocus = this;

#ifdef DISTRHO_OS_WINDOWS
		//commenting this out for the right-click menu, since we want to position the window ourself

		/* // Center this window
		PuglInternals *const parentImpl = fModal.parent->fView->impl;

		RECT curRect;
		RECT parentRect;
		GetWindowRect(hwnd, &curRect);
		GetWindowRect(parentImpl->hwnd, &parentRect);

		int x = parentRect.left + (parentRect.right - curRect.right) / 2;
		int y = parentRect.top + (parentRect.bottom - curRect.bottom) / 2;

		SetWindowPos(hwnd, 0, x, y, 0, 0, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOZORDER);
		UpdateWindow(hwnd); */
#endif

		fModal.parent->setVisible(true);
		setVisible(true);

		DBG("Ok\n");
	}

	void exec_fini()
	{
		DBG("Window modal loop stopping...");
		DBGF;
		fModal.enabled = false;

		if (fModal.parent != nullptr)
		{
			fModal.parent->fModal.childFocus = nullptr;

			// the mouse position probably changed since the modal appeared,
			// so send a mouse motion event to the modal's parent window
#if defined(DISTRHO_OS_WINDOWS)
			// TODO
#elif defined(DISTRHO_OS_MAC)
			// TODO
#else
			int i, wx, wy;
			uint u;
			::Window w;
			if (XQueryPointer(fModal.parent->xDisplay, fModal.parent->xWindow, &w, &w, &i, &i, &wx, &wy, &u) == True)
				fModal.parent->onPuglMotion(wx, wy);
#endif
		}

		DBG("Ok\n");
	}

	void focus()
	{
		DBG("Window focus\n");
#if defined(DISTRHO_OS_WINDOWS)
		SetForegroundWindow(hwnd);
		SetActiveWindow(hwnd);
		SetFocus(hwnd);
#elif defined(DISTRHO_OS_MAC)
		if (mWindow != nullptr)
		{
			// TODO
			//[NSApp activateIgnoringOtherApps:YES];
			//[mWindow makeKeyAndOrderFront:mWindow];
		}
#else				
		XRaiseWindow(xDisplay, xWindow);
		XSetInputFocus(xDisplay, xWindow, RevertToPointerRoot, CurrentTime);
		XFlush(xDisplay);
#endif
	}

	// -------------------------------------------------------------------

	void setVisible(const bool yesNo)
	{
		if (fVisible == yesNo)
		{
			DBG("Window setVisible matches current state, ignoring request\n");
			return;
		}
		if (fUsingEmbed)
		{
			DBG("Window setVisible cannot be called when embedded\n");
			return;
		}

		DBG("Window setVisible called\n");

		fVisible = yesNo;

		if (yesNo && fFirstInit)
			setSize(fWidth, fHeight, true);

#if defined(DISTRHO_OS_WINDOWS)
		if (yesNo)
			ShowWindow(hwnd, fFirstInit ? SW_SHOWNORMAL : SW_RESTORE);
		else
			ShowWindow(hwnd, SW_HIDE);

		UpdateWindow(hwnd);
#elif defined(DISTRHO_OS_MAC)
		if (yesNo)
		{
			if (mWindow != nullptr)
				[mWindow setIsVisible:YES];
			else
				[mView setHidden:NO];
		}
		else
		{
			if (mWindow != nullptr)
				[mWindow setIsVisible:NO];
			else
				[mView setHidden:YES];
		}
#else
		if (yesNo)
			XMapRaised(xDisplay, xWindow);
		else
			XUnmapWindow(xDisplay, xWindow);

		XFlush(xDisplay);
#endif

		if (yesNo)
		{
			if (fFirstInit)
			{
				fApp.pData->oneShown();
				fFirstInit = false;
			}
		}
		else if (fModal.enabled)
			exec_fini();
	}

	// -------------------------------------------------------------------

	void setResizable(const bool yesNo)
	{
		if (fResizable == yesNo)
		{
			DBG("Window setResizable matches current state, ignoring request\n");
			return;
		}
		if (fUsingEmbed)
		{
			DBG("Window setResizable cannot be called when embedded\n");
			return;
		}

		DBG("Window setResizable called\n");

		fResizable = yesNo;

#if defined(DISTRHO_OS_WINDOWS)
		const int winFlags = fResizable ? GetWindowLong(hwnd, GWL_STYLE) | WS_SIZEBOX
										: GetWindowLong(hwnd, GWL_STYLE) & ~WS_SIZEBOX;
		SetWindowLong(hwnd, GWL_STYLE, winFlags);
#elif defined(DISTRHO_OS_MAC)
		const uint flags(yesNo ? (NSViewWidthSizable | NSViewHeightSizable) : 0x0);
		[mView setAutoresizingMask:flags];
#endif

		setSize(fWidth, fHeight, true);
	}

	// -------------------------------------------------------------------

	void setSize(uint width, uint height, const bool forced = false)
	{
		if (width <= 1 || height <= 1)
		{
			DBGp("Window setSize called with invalid value(s) %i %i, ignoring request\n", width, height);
			return;
		}

		if (fWidth == width && fHeight == height && !forced)
		{
			DBGp("Window setSize matches current size, ignoring request (%i %i)\n", width, height);
			return;
		}

		fWidth = width;
		fHeight = height;

		DBGp("Window setSize called %s, size %i %i, resizable %s\n", forced ? "(forced)" : "(not forced)", width, height, fResizable ? "true" : "false");

#if defined(DISTRHO_OS_WINDOWS)
		const int winFlags = WS_POPUPWINDOW | WS_CAPTION | (fResizable ? WS_SIZEBOX : 0x0);
		RECT wr = {0, 0, static_cast<long>(width), static_cast<long>(height)};
		AdjustWindowRectEx(&wr, fUsingEmbed ? WS_CHILD : winFlags, FALSE, WS_EX_TOPMOST);

		SetWindowPos(hwnd, 0, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
					 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);

		if (!forced)
			UpdateWindow(hwnd);
#elif defined(DISTRHO_OS_MAC)
		[mView setFrame:NSMakeRect(0, 0, width, height)];

		if (mWindow != nullptr)
		{
			const NSSize size = NSMakeSize(width, height);
			[mWindow setContentSize:size];

			if (fResizable)
			{
				[mWindow setContentMinSize:NSMakeSize(1, 1)];
				[mWindow setContentMaxSize:NSMakeSize(99999, 99999)];
				[[mWindow standardWindowButton:NSWindowZoomButton] setHidden:NO];
			}
			else
			{
				[mWindow setContentMinSize:size];
				[mWindow setContentMaxSize:size];
				[[mWindow standardWindowButton:NSWindowZoomButton] setHidden:YES];
			}
		}
#else
		XResizeWindow(xDisplay, xWindow, width, height);

		if (!fResizable)
		{
			XSizeHints sizeHints;
			memset(&sizeHints, 0, sizeof(sizeHints));

			sizeHints.flags = PSize | PMinSize | PMaxSize;
			sizeHints.width = static_cast<int>(width);
			sizeHints.height = static_cast<int>(height);
			sizeHints.min_width = static_cast<int>(width);
			sizeHints.min_height = static_cast<int>(height);
			sizeHints.max_width = static_cast<int>(width);
			sizeHints.max_height = static_cast<int>(height);

			XSetNormalHints(xDisplay, xWindow, &sizeHints);
		}

		if (!forced)
			XFlush(xDisplay);
#endif

		puglPostRedisplay(fView);
	}

	// -------------------------------------------------------------------

	const char *getTitle() const noexcept
	{
		static const char *const kFallback = "";

		return fTitle != nullptr ? fTitle : kFallback;
	}

	void setTitle(const char *const title)
	{
		DBGp("Window setTitle \"%s\"\n", title);

		if (fTitle != nullptr)
			std::free(fTitle);

		fTitle = strdup(title);

#if defined(DISTRHO_OS_WINDOWS)
		SetWindowTextA(hwnd, title);
#elif defined(DISTRHO_OS_MAC)
		if (mWindow != nullptr)
		{
			NSString *titleString = [[NSString alloc]
				initWithBytes:title
					   length:strlen(title)
					 encoding:NSUTF8StringEncoding];

			[mWindow setTitle:titleString];
		}
#else
		XStoreName(xDisplay, xWindow, title);
#endif
	}

	void setTransientWinId(const uintptr_t winId)
	{
		DISTRHO_SAFE_ASSERT_RETURN(winId != 0, );

#if defined(DISTRHO_OS_WINDOWS)
		// TODO
#elif defined(DISTRHO_OS_MAC)
		NSWindow *const window = [NSApp windowWithWindowNumber:winId];
		DISTRHO_SAFE_ASSERT_RETURN(window != nullptr, );

		[window addChildWindow:mWindow
					   ordered:NSWindowAbove];
		[mWindow makeKeyWindow];
#else
		XSetTransientForHint(xDisplay, xWindow, static_cast<::Window>(winId));
#endif
	}

	// -------------------------------------------------------------------

	void addWidget(Widget *const widget)
	{
		fWidgets.push_back(widget);
	}

	void removeWidget(Widget *const widget)
	{
		fWidgets.remove(widget);
	}

	void idle()
	{
		puglProcessEvents(fView);

#ifdef DISTRHO_OS_MAC
		if (fNeedsIdle)
		{
			NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
			NSEvent *event;

			for (;;)
			{
				event = [NSApp
					nextEventMatchingMask:NSAnyEventMask
								untilDate:[NSDate distantPast]
								   inMode:NSDefaultRunLoopMode
								  dequeue:YES];

				if (event == nil)
					break;

				[NSApp sendEvent:event];
			}

			[pool release];
		}
#endif

		if (fModal.enabled && fModal.parent != nullptr)
			fModal.parent->idle();
	}

	// -------------------------------------------------------------------

	void onPuglDisplay()
	{
		fSelf->onDisplayBefore();

		FOR_EACH_WIDGET(it)
		{
			Widget *const widget(*it);
			widget->pData->display(fWidth, fHeight);
		}

		fSelf->onDisplayAfter();
	}

	int onPuglKeyboard(const bool press, const uint key)
	{
		DBGp("PUGL: onKeyboard : %i %i\n", press, key);

		if (fModal.childFocus != nullptr)
		{
			fModal.childFocus->focus();
			return 0;
		}

		Widget::KeyboardEvent ev;
		ev.press = press;
		ev.key = key;
		//ev.mod = static_cast<Modifier>(puglGetModifiers(fView));
		//ev.time = puglGetEventTimestamp(fView);

		FOR_EACH_WIDGET_INV(rit)
		{
			Widget *const widget(*rit);

			if (widget->isVisible() && widget->onKeyboard(ev))
				return 0;
		}

		return 1;
	}

	int onPuglSpecial(const bool press, const Key key)
	{
		DBGp("PUGL: onSpecial : %i %i\n", press, key);

		if (fModal.childFocus != nullptr)
		{
			fModal.childFocus->focus();
			return 0;
		}

		Widget::SpecialEvent ev;
		ev.press = press;
		ev.key = key;
		//ev.mod = static_cast<Modifier>(puglGetModifiers(fView));
		//ev.time = puglGetEventTimestamp(fView);

		FOR_EACH_WIDGET_INV(rit)
		{
			Widget *const widget(*rit);

			if (widget->isVisible() && widget->onSpecial(ev))
				return 0;
		}

		return 1;
	}

	void onPuglMouse(const int button, const bool press, const int x, const int y)
	{
		DBGp("PUGL: onMouse : %i %i %i %i\n", button, press, x, y);

		// FIXME - pugl sends 2 of these for each window on init, don't ask me why. we'll ignore it
		if (press && button == 0 && x == 0 && y == 0)
			return;

		if (fModal.childFocus != nullptr)
		{
			//this would cause some issues with right-click menu
			//return fModal.childFocus->focus();
		}

		Widget::MouseEvent ev;
		ev.button = button;
		ev.press = press;
		//ev.mod = static_cast<Modifier>(puglGetModifiers(fView));
		//ev.time = puglGetEventTimestamp(fView);

		FOR_EACH_WIDGET_INV(rit)
		{
			Widget *const widget(*rit);

			ev.pos = Point<int>(x - widget->getAbsoluteX(), y - widget->getAbsoluteY());

			if (widget->isVisible() && widget->onMouse(ev))
				break;
		}

		if (fIsContextMenu && ev.press)
		{
			close();
		}
	}

	void onPuglMotion(const int x, const int y)
	{
		DBGp("PUGL: onMotion : %i %i\n", x, y);

		if (fModal.childFocus != nullptr)
			return;

		Widget::MotionEvent ev;
		//ev.mod = static_cast<Modifier>(puglGetModifiers(fView));
		//ev.time = puglGetEventTimestamp(fView);

		FOR_EACH_WIDGET_INV(rit)
		{
			Widget *const widget(*rit);

			ev.pos = Point<int>(x - widget->getAbsoluteX(), y - widget->getAbsoluteY());

			if (widget->isVisible() && widget->onMotion(ev))
				break;
		}
	}

	void onPuglScroll(const int x, const int y, const float dx, const float dy)
	{
		DBGp("PUGL: onScroll : %i %i %f %f\n", x, y, dx, dy);

		if (fModal.childFocus != nullptr)
			return;

		Widget::ScrollEvent ev;
		ev.delta = Point<float>(dx, dy);
		//ev.mod = static_cast<Modifier>(puglGetModifiers(fView));
		//ev.time = puglGetEventTimestamp(fView);

		FOR_EACH_WIDGET_INV(rit)
		{
			Widget *const widget(*rit);

			ev.pos = Point<int>(x - widget->getAbsoluteX(), y - widget->getAbsoluteY());

			if (widget->isVisible() && widget->onScroll(ev))
				break;
		}
	}

	void onPuglReshape(const int width, const int height)
	{
		DBGp("PUGL: onReshape : %i %i\n", width, height);

		if (width <= 1 && height <= 1)
			return;

		fWidth = static_cast<uint>(width);
		fHeight = static_cast<uint>(height);

		fSelf->onReshape(fWidth, fHeight);

		FOR_EACH_WIDGET(it)
		{
			Widget *const widget(*it);

			if (widget->pData->needsFullViewport)
				widget->setSize(fWidth, fHeight);
		}
	}

	void onPuglClose()
	{
		DBG("PUGL: onClose\n");

		if (fModal.enabled)
			exec_fini();

		fSelf->onClose();

		if (fModal.childFocus != nullptr)
			fModal.childFocus->fSelf->onClose();

		close();
	}

	void onPuglFocusOut()
	{
		DBG("PUGL: onFocusOut\n");

		fSelf->onFocusOut();

		FOR_EACH_WIDGET_INV(rit)
		{
			Widget *const widget(*rit);

			widget->onFocusOut();
		}

		fSelf->unclipCursor();
	}
	
	// -------------------------------------------------------------------

	bool handlePluginKeyboard(const bool press, const uint key)
	{
		DBGp("PUGL: handlePluginKeyboard : %i %i\n", press, key);

		if (fModal.childFocus != nullptr)
		{
			fModal.childFocus->focus();
			return true;
		}

		Widget::KeyboardEvent ev;
		ev.press = press;
		ev.key = key;
		//ev.mod = static_cast<Modifier>(fView->mods);
		ev.time = 0;

		if ((ev.mod & kModifierShift) != 0 && ev.key >= 'a' && ev.key <= 'z')
			ev.key -= 'a' - 'A'; // a-z -> A-Z

		FOR_EACH_WIDGET_INV(rit)
		{
			Widget *const widget(*rit);

			if (widget->isVisible() && widget->onKeyboard(ev))
				return true;
		}

		return false;
	}

	bool handlePluginSpecial(const bool press, const Key key)
	{
		DBGp("PUGL: handlePluginSpecial : %i %i\n", press, key);

		if (fModal.childFocus != nullptr)
		{
			fModal.childFocus->focus();
			return true;
		}

		int mods = 0x0;

		switch (key)
		{
		case kKeyShift:
			mods |= kModifierShift;
			break;
		case kKeyControl:
			mods |= kModifierControl;
			break;
		case kKeyAlt:
			mods |= kModifierAlt;
			break;
		default:
			break;
		}

		/*if (mods != 0x0)
		{
			if (press)
				fView->mods |= mods;
			else
				fView->mods &= ~(mods);
		}*/

		Widget::SpecialEvent ev;
		ev.press = press;
		ev.key = key;
		//ev.mod = static_cast<Modifier>(fView->mods);
		ev.time = 0;

		FOR_EACH_WIDGET_INV(rit)
		{
			Widget *const widget(*rit);

			if (widget->isVisible() && widget->onSpecial(ev))
				return true;
		}

		return false;
	}

	// -------------------------------------------------------------------

	Application &fApp;
	Window *fSelf;
	PuglView *fView;

	bool fFirstInit;
	bool fVisible;
	bool fResizable;
	bool fUsingEmbed;
	uint fWidth;
	uint fHeight;
	char *fTitle;
	std::list<Widget *> fWidgets;

	//fork---------
	bool fCursorIsClipped;
	bool fMustSaveSize;
	bool fIsFullscreen;
	Size<uint> fPreFullscreenSize;
	bool fIsContextMenu;
	//-------------

	struct Modal
	{
		bool enabled;
		PrivateData *parent;
		PrivateData *childFocus;

		Modal()
			: enabled(false),
			  parent(nullptr),
			  childFocus(nullptr) {}

		Modal(PrivateData *const p)
			: enabled(false),
			  parent(p),
			  childFocus(nullptr) {}

		~Modal()
		{
			DISTRHO_SAFE_ASSERT(!enabled);
			DISTRHO_SAFE_ASSERT(childFocus == nullptr);
		}

		DISTRHO_DECLARE_NON_COPY_STRUCT(Modal)
	} fModal;

#if defined(DISTRHO_OS_WINDOWS)
	HWND hwnd;
#elif defined(DISTRHO_OS_MAC)
	bool fNeedsIdle;
	PuglOpenGLView *mView;
	id mWindow;
#else
	Display *xDisplay;

	::Window xWindow;

	//fork---------
	::Window xClipCursorWindow;
	Cursor invisibleCursor;
	//-------------
#endif

	// -------------------------------------------------------------------
	// Callbacks

#define handlePtr ((PrivateData *)puglGetHandle(view))

	static void onDisplayCallback(PuglView *view)
	{
		handlePtr->onPuglDisplay();
	}

	static void onEventCallback(PuglView *view, const PuglEvent *event)
	{
		switch (event->type)
		{
		case PUGL_NOTHING:
			break;
		case PUGL_CONFIGURE:
			onReshapeCallback(view, event->configure.width, event->configure.height);
			break;
		case PUGL_EXPOSE:
			onDisplayCallback(view);
			break;
		case PUGL_KEY_PRESS:
			onKeyboardCallback(view, true, event->key.keycode);
			break;
		case PUGL_KEY_RELEASE:
			onKeyboardCallback(view, false, event->key.keycode);
			break;
		case PUGL_CLOSE:
			onCloseCallback(view);
			break;
		case PUGL_MOTION_NOTIFY:
			onMotionCallback(view, event->motion.x, event->motion.y);
			break;
		case PUGL_BUTTON_PRESS:
			onMouseCallback(view, event->button.button, true, event->button.x, event->button.y);
			break;
		case PUGL_BUTTON_RELEASE:
			onMouseCallback(view, event->button.button, false, event->button.x, event->button.y);
			break;
		case PUGL_SCROLL:
			onScrollCallback(view, event->scroll.x, event->scroll.y, event->scroll.dx, event->scroll.dy);
			break;
		case PUGL_FOCUS_OUT:
			onFocusOutCallback(view);
			break;
		default:
			break;
		}
	}

	static int onKeyboardCallback(PuglView *view, bool press, uint32_t key)
	{
		return handlePtr->onPuglKeyboard(press, key);
	}

	static int onSpecialCallback(PuglView *view, bool press, PuglKey key)
	{
		return handlePtr->onPuglSpecial(press, static_cast<Key>(key));
	}

	static void onMouseCallback(PuglView *view, int button, bool press, int x, int y)
	{
		handlePtr->onPuglMouse(button, press, x, y);
	}

	static void onMotionCallback(PuglView *view, int x, int y)
	{
		handlePtr->onPuglMotion(x, y);
	}

	static void onScrollCallback(PuglView *view, int x, int y, float dx, float dy)
	{
		handlePtr->onPuglScroll(x, y, dx, dy);
	}

	static void onReshapeCallback(PuglView *view, int width, int height)
	{
		handlePtr->onPuglReshape(width, height);
	}

	static void onCloseCallback(PuglView *view)
	{
		handlePtr->onPuglClose();
	}

	static void onFocusOutCallback(PuglView *view)
	{
		handlePtr->onPuglFocusOut();
	}

#ifndef DGL_FILE_BROWSER_DISABLED
	static void fileBrowserSelectedCallback(PuglView *view, const char *filename)
	{
		handlePtr->fSelf->fileBrowserSelected(filename);
	}
#endif

#undef handlePtr

	DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PrivateData)
};

// -----------------------------------------------------------------------
// Window

Window::Window(Application &app)
	: pData(new PrivateData(app, this)) {}

Window::Window(Application &app, Window &parent)
	: pData(new PrivateData(app, this, parent)) {}

Window::Window(Application &app, intptr_t parentId)
	: pData(new PrivateData(app, this, parentId)) {}

Window::~Window()
{
	delete pData;
}

void Window::show()
{
	pData->setVisible(true);
}

void Window::hide()
{
	pData->setVisible(false);
}

void Window::close()
{
	pData->close();
}

void Window::exec(bool lockWait)
{
	pData->exec(lockWait);
}

void Window::focus()
{
	pData->focus();
}

void Window::repaint() noexcept
{
	puglPostRedisplay(pData->fView);
}

	// static int fib_filter_filename_filter(const char* const name)
	// {
	//     return 1;
	//     (void)name;
	// }

#ifndef DGL_FILE_BROWSER_DISABLED
bool Window::openFileBrowser(const FileBrowserOptions &options)
{
#ifdef SOFD_HAVE_X11
	using DISTRHO_NAMESPACE::String;

	// --------------------------------------------------------------------------
	// configure start dir

	// TODO: get abspath if needed
	// TODO: cross-platform

	String startDir(options.startDir);

	if (startDir.isEmpty())
	{
		if (char *const dir_name = get_current_dir_name())
		{
			startDir = dir_name;
			std::free(dir_name);
		}
	}

	DISTRHO_SAFE_ASSERT_RETURN(startDir.isNotEmpty(), false);

	if (!startDir.endsWith('/'))
		startDir += "/";

	DISTRHO_SAFE_ASSERT_RETURN(x_fib_configure(0, startDir) == 0, false);

	// --------------------------------------------------------------------------
	// configure title

	String title(options.title);

	if (title.isEmpty())
	{
		title = pData->getTitle();

		if (title.isEmpty())
			title = "FileBrowser";
	}

	DISTRHO_SAFE_ASSERT_RETURN(x_fib_configure(1, title) == 0, false);

	// --------------------------------------------------------------------------
	// configure filters

	x_fib_cfg_filter_callback(nullptr); //fib_filter_filename_filter);

	// --------------------------------------------------------------------------
	// configure buttons

	x_fib_cfg_buttons(3, options.buttons.listAllFiles - 1);
	x_fib_cfg_buttons(1, options.buttons.showHidden - 1);
	x_fib_cfg_buttons(2, options.buttons.showPlaces - 1);

	// --------------------------------------------------------------------------
	// show

	return (x_fib_show(pData->xDisplay, pData->xWindow, /*options.width*/ 0, /*options.height*/ 0) == 0);
#else
	// not implemented
	return false;
#endif
}
#endif

bool Window::isEmbed() const noexcept
{
    return pData->fUsingEmbed;
}

bool Window::isVisible() const noexcept
{
	return pData->fVisible;
}

void Window::setVisible(bool yesNo)
{
	pData->setVisible(yesNo);
}

bool Window::isResizable() const noexcept
{
	return pData->fResizable;
}

void Window::setResizable(bool yesNo)
{
	pData->setResizable(yesNo);
}

uint Window::getWidth() const noexcept
{
	return pData->fWidth;
}

uint Window::getHeight() const noexcept
{
	return pData->fHeight;
}

Size<uint> Window::getSize() const noexcept
{
	return Size<uint>(pData->fWidth, pData->fHeight);
}

void Window::setSize(uint width, uint height)
{
	pData->setSize(width, height);
}

void Window::setSize(Size<uint> size)
{
	pData->setSize(size.getWidth(), size.getHeight());
}

const char *Window::getTitle() const noexcept
{
	return pData->getTitle();
}

void Window::setTitle(const char *title)
{
	pData->setTitle(title);
}

void Window::setTransientWinId(uintptr_t winId)
{
	pData->setTransientWinId(winId);
}

Application &Window::getApp() const noexcept
{
	return pData->fApp;
}

intptr_t Window::getWindowId() const noexcept
{
	return puglGetNativeWindow(pData->fView);
}

void Window::_addWidget(Widget *const widget)
{
	pData->addWidget(widget);
}

void Window::_removeWidget(Widget *const widget)
{
	pData->removeWidget(widget);
}

void Window::_idle()
{
	pData->idle();
}

// -----------------------------------------------------------------------

void Window::addIdleCallback(IdleCallback *const callback)
{
	DISTRHO_SAFE_ASSERT_RETURN(callback != nullptr, )

	pData->fApp.pData->idleCallbacks.push_back(callback);
}

void Window::removeIdleCallback(IdleCallback *const callback)
{
	DISTRHO_SAFE_ASSERT_RETURN(callback != nullptr, )

	pData->fApp.pData->idleCallbacks.remove(callback);
}

// -----------------------------------------------------------------------

void Window::onDisplayBefore()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glLoadIdentity();
}

void Window::onDisplayAfter()
{
}

void Window::onReshape(uint width, uint height)
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, static_cast<GLdouble>(width), static_cast<GLdouble>(height), 0.0, 0.0, 1.0);
	glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void Window::onFocusOut()
{

}

void Window::onClose()
{
}

//fork----------

void Window::setMinSize(uint width, uint height)
{
#if defined(DISTRHO_OS_MAC)
	[pData->mWindow setContentMinSize:NSMakeSize(width, height)];
#elif !defined(DISTRHO_OS_WINDOWS) //Linux
	XSizeHints sizeHints;
	memset(&sizeHints, 0, sizeof(sizeHints));

	sizeHints.flags = PMinSize;
	sizeHints.min_width = static_cast<int>(width);
	sizeHints.min_height = static_cast<int>(height);

	XSetNormalHints(pData->xDisplay, pData->xWindow, &sizeHints);
#endif	

	//pugl takes care of it for windows
	pData->fView->min_width = width; 
	pData->fView->min_height = height;
}

Point<int> Window::getAbsolutePos()
{
	int posX;
	int posY;

#if !defined(DISTRHO_OS_WINDOWS) && !defined(DISTRHO_OS_MAC)
	::Window unused;

	XTranslateCoordinates(pData->xDisplay,
                      pData->xWindow,
                      DefaultRootWindow(pData->xDisplay),
                      0, 0,
                      &posX,
                      &posY,
					  &unused);
						
	return Point<int>(posX, posY);
#elif defined(DISTRHO_OS_WINDOWS)
	RECT windowRect;
    GetWindowRect(pData->hwnd, &windowRect);

	return Point<int>(windowRect.left, windowRect.top);
#endif
}

void Window::setAbsolutePos(const uint x, const uint y)
{
#if !defined(DISTRHO_OS_WINDOWS) && !defined(DISTRHO_OS_MAC)
	XMoveWindow(pData->xDisplay, pData->xWindow, x, y);

#elif defined(DISTRHO_OS_WINDOWS)	
	SetWindowPos(pData->hwnd, HWND_TOP, x, y, getWidth(), getHeight(), isVisible() ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);

#endif
}

//TODO: proper "ContextWindow" class, or similar
void Window::hideFromTaskbar()
{
#if !defined(DISTRHO_OS_WINDOWS) && !defined(DISTRHO_OS_MAC)
	Atom wmState = XInternAtom(pData->xDisplay,  "_NET_WM_STATE", False);
	Atom atom = XInternAtom(pData->xDisplay, "_NET_WM_STATE_SKIP_TASKBAR", False);

	XChangeProperty(pData->xDisplay, pData->xWindow, wmState, XA_ATOM, 32, PropModeReplace, (unsigned char *)&atom, 1);

	XSetWindowAttributes attributes;
	attributes.override_redirect = true;
	XChangeWindowAttributes(pData->xDisplay, pData->xWindow, CWOverrideRedirect, &attributes);
	XGrabPointer(pData->xDisplay, pData->xWindow, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	
#elif defined(DISTRHO_OS_WINDOWS)	
	SetWindowLong(pData->hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_APPWINDOW);
#endif

	pData->fIsContextMenu = true;
}

void Window::setBorderless(bool borderless)
{
#if !defined(DISTRHO_OS_WINDOWS) && !defined(DISTRHO_OS_MAC)
	struct MwmHints {
    	unsigned long flags;
    	unsigned long functions;
    	unsigned long decorations;
    	long input_mode;
    	unsigned long status;
	};
	enum {
    	MWM_HINTS_FUNCTIONS = (1L << 0),
    	MWM_HINTS_DECORATIONS =  (1L << 1),

		MWM_FUNC_ALL = (1L << 0),
		MWM_FUNC_RESIZE = (1L << 1),
		MWM_FUNC_MOVE = (1L << 2),
		MWM_FUNC_MINIMIZE = (1L << 3),
		MWM_FUNC_MAXIMIZE = (1L << 4),
		MWM_FUNC_CLOSE = (1L << 5)
	};

	Atom mwmHintsProperty = XInternAtom(pData->xDisplay, "_MOTIF_WM_HINTS", 0);
	struct MwmHints hints;
	hints.flags = MWM_HINTS_DECORATIONS;
	hints.decorations = borderless ? 0 : 1;

	XChangeProperty(pData->xDisplay, pData->xWindow, mwmHintsProperty, mwmHintsProperty, 32, PropModeReplace, (unsigned char *)&hints, 5);
	
#elif defined(DISTRHO_OS_WINDOWS)	
	LONG lStyle = GetWindowLong(pData->hwnd, GWL_STYLE);
	lStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
	SetWindowLong(pData->hwnd, GWL_STYLE, lStyle);
#endif
}

void Window::toggleFullscreen()
{
#if !defined(DISTRHO_OS_WINDOWS) && !defined(DISTRHO_OS_MAC)
	XUnmapWindow(pData->xDisplay, pData->xWindow);
	XSync(pData->xDisplay, False);

	Atom atoms[2] = { XInternAtom(pData->xDisplay, "_NET_WM_STATE_FULLSCREEN", False), None };
	XChangeProperty(pData->xDisplay, pData->xWindow, XInternAtom(pData->xDisplay, "_NET_WM_STATE", False), XA_ATOM, 32, PropModeReplace, (unsigned char *)atoms, 1);
	XSync(pData->xDisplay, False);

	XMapWindow(pData->xDisplay, pData->xWindow);
	XSync(pData->xDisplay, False);

	int screen = 0;

	if(!pData->fIsFullscreen)
	{
		pData->fPreFullscreenSize = getSize();
		setSize(XWidthOfScreen(XScreenOfDisplay(pData->xDisplay, screen)), XHeightOfScreen(XScreenOfDisplay(pData->xDisplay, screen)));
	}
	else
	{
		setSize(pData->fPreFullscreenSize);
	}
	
#endif

	saveSizeAtExit(false); //to make sure the default window size won't be as big as the monitor 
	pData->fIsFullscreen = !pData->fIsFullscreen;
}

void Window::saveSizeAtExit(bool yesno)
{
	pData->fMustSaveSize = yesno;
}

bool Window::mustSaveSize()
{
	return pData->fMustSaveSize;
}

void Window::setCursorStyle(CursorStyle style) noexcept
{
#if defined(DISTRHO_OS_WINDOWS)	
	LPCSTR cursorName;

	switch (style)
	{
	case CursorStyle::Default:
		cursorName = IDC_ARROW;
		break;
	case CursorStyle::Grab:
		cursorName = IDC_HAND;
		break;
	case CursorStyle::Pointer:
		cursorName = IDC_HAND;
		break;
	case CursorStyle::SouthEastResize:
		cursorName = IDC_SIZENWSE;
		break;
	case CursorStyle::UpDown:
		cursorName = IDC_SIZENS;
		break;
	default:
		cursorName = IDC_ARROW;
		break;
	}
	
	HCURSOR cursor = LoadCursor(NULL, cursorName);
	SetCursor(cursor);

#elif defined(DISTRHO_OS_MAC)

	switch (style)
	{
	case CursorStyle::Default:
		[[NSCursor arrow] set];
		break;
	case CursorStyle::Grab:
		[[NSCursor openHand] set];
		break;
	case CursorStyle::Pointer:
		[[NSCursor pointingHand] set];
		break;
	case CursorStyle::SouthEastResize:
		[[NSCursor _windowResizeNorthWestSouthEastCursor] set];
		break;
	case CursorStyle::UpDown:
		[[NSCursor resizeUpDown] set];
		break;
	default:
		[[NSCursor arrow] set];
		break;
	}

#else
	uint cursorId;

	switch (style)
	{
	case CursorStyle::Default:
		cursorId = XC_arrow;
		break;
	case CursorStyle::Grab:
		cursorId = XC_hand2;
		break;
	case CursorStyle::Pointer:
		cursorId = XC_hand2;
		break;
	case CursorStyle::SouthEastResize:
		cursorId = XC_bottom_right_corner;
		break;
	case CursorStyle::UpDown:
		cursorId = XC_sb_v_double_arrow;
		break;
	default:
		cursorId = XC_arrow;
		break;
	}

	Cursor cursor = XCreateFontCursor(pData->xDisplay, cursorId); 
	XDefineCursor(pData->xDisplay, pData->xWindow, cursor);

	XSync(pData->xDisplay, False);
#endif
}

void Window::showCursor() noexcept
{
#if defined(DISTRHO_OS_WINDOWS)
	while (ShowCursor(true) < 0)
		;

#elif defined(DISTRHO_OS_MAC)
	CGDisplayShowCursor(kCGNullDirectDisplay);

#else
	XUndefineCursor(pData->xDisplay, pData->xWindow);

	XSync(pData->xDisplay, False);
#endif
}

void Window::hideCursor() noexcept
{
#if defined(DISTRHO_OS_WINDOWS)
	while (ShowCursor(false) >= 0)
		;

#elif defined(DISTRHO_OS_MAC)
	CGDisplayHideCursor(kCGNullDirectDisplay);

#else
	XDefineCursor(pData->xDisplay, pData->xWindow, pData->invisibleCursor);

	XSync(pData->xDisplay, False);
#endif
}

const Point<int> Window::getCursorPos() const noexcept
{
#if defined(DISTRHO_OS_WINDOWS)
	POINT pos;
	GetCursorPos(&pos);

	ScreenToClient(pData->hwnd, &pos);

	return Point<int>(pos.x, pos.y);

#elif defined(DISTRHO_OS_MAC)
	NSPoint mouseLoc = [NSEvent mouseLocation];

	const int x = static_cast<int>(mouseLoc.x);
	const int y = static_cast<int>(pData->fHeight - mouseLoc.y); //flip y so that the origin is at the top left

	fprintf(stderr, "%d %d\n", x, y);
	return Point<int>(x, y);

#else
	int posX, posY;

	//unused variables
	int i;
	uint u;
	::Window w;

	XQueryPointer(pData->xDisplay, pData->xWindow, &w, &w, &i, &i, &posX, &posY, &u);

	return Point<int>(posX, posY);
#endif
}

/**
 * Set the cursor position relative to the window.
 */
void Window::setCursorPos(int x, int y) noexcept
{
#if defined(DISTRHO_OS_WINDOWS)
	RECT winRect;
	GetWindowRect(pData->hwnd, &winRect);

	SetCursorPos(winRect.left + x, winRect.top + y);

#elif defined(DISTRHO_OS_MAC)
	CGWarpMouseCursorPosition(CGPointMake(x, y));

#else
	Display *xDisplay = pData->xDisplay;	
	XEvent xEvent;

	XSynchronize(xDisplay, True);

	XWarpPointer(xDisplay, None, pData->xWindow, 0, 0, 0, 0, x, y);

	while (XPending(xDisplay) > 0) 
	{
		XNextEvent(xDisplay, &xEvent);

		if (xEvent.type == ButtonRelease) 
		{
			PuglEvent event = translateEvent(pData->fView, xEvent);
			pData->onMouseCallback(pData->fView, event.button.button, event.button.state, event.button.x, event.button.y);
		} 
	}

	XSynchronize(xDisplay, False);
#endif
}

void Window::setCursorPos(const Point<int> &pos) noexcept
{
	setCursorPos(pos.getX(), pos.getY());
}

void Window::setCursorPos(Widget *const widget) noexcept
{
	setCursorPos(widget->getAbsoluteX() + widget->getWidth() / 2, widget->getAbsoluteY() + widget->getHeight() / 2);
}

void Window::clipCursor(Rectangle<int> rect) const noexcept
{
	pData->fCursorIsClipped = true;

#if defined(DISTRHO_OS_WINDOWS)
	RECT winRect, clipRect;
	GetWindowRect(pData->hwnd, &winRect);

	clipRect.left = rect.getX() + winRect.left;
	clipRect.right = rect.getX() + rect.getWidth() + winRect.left + 1;
	clipRect.top = rect.getY() + winRect.top;
	clipRect.bottom = rect.getY() + rect.getHeight() + winRect.top + 1;

	ClipCursor(&clipRect);

#elif defined(DISTRHO_OS_MAC)
	//CGAssociateMouseAndMouseCursorPosition(false);

#else
	XMoveResizeWindow(pData->xDisplay, pData->xClipCursorWindow, rect.getX(), rect.getY(), rect.getWidth() + 1, rect.getHeight() + 1);
	XSync(pData->xDisplay, False);

	XGrabPointer(pData->xDisplay, pData->xWindow, True, 0, GrabModeAsync, GrabModeAsync, pData->xClipCursorWindow, None, CurrentTime);
	XSync(pData->xDisplay, False);
#endif
}

void Window::clipCursor(Widget *const widget) const noexcept
{
	const Point<int> pos = widget->getAbsolutePos();
	const uint width = widget->getWidth();
	const uint height = widget->getHeight();

	clipCursor(Rectangle<int>(pos, width, height));
}

void Window::unclipCursor() const noexcept
{
	pData->fCursorIsClipped = false;

#if defined(DISTRHO_OS_WINDOWS)
	ClipCursor(NULL);

#elif defined(DISTRHO_OS_MAC)
	CGAssociateMouseAndMouseCursorPosition(true);

#else
	XUngrabPointer(pData->xDisplay, CurrentTime);

	XSync(pData->xDisplay, False);
#endif
}

	//end fork------

#ifndef DGL_FILE_BROWSER_DISABLED
void Window::fileBrowserSelected(const char *)
{
}
#endif

bool Window::handlePluginKeyboard(const bool press, const uint key)
{
	return pData->handlePluginKeyboard(press, key);
}

bool Window::handlePluginSpecial(const bool press, const Key key)
{
	return pData->handlePluginSpecial(press, key);
}

// -----------------------------------------------------------------------

StandaloneWindow::StandaloneWindow()
	: Application(),
	  Window((Application &)*this),
	  fWidget(nullptr) {}

void StandaloneWindow::exec()
{
	Window::show();
	Application::exec();
}

void StandaloneWindow::onReshape(uint width, uint height)
{
	if (fWidget != nullptr)
		fWidget->setSize(width, height);
	Window::onReshape(width, height);
}

void StandaloneWindow::_addWidget(Widget *widget)
{
	if (fWidget == nullptr)
	{
		fWidget = widget;
		fWidget->pData->needsFullViewport = true;
	}
	Window::_addWidget(widget);
}

void StandaloneWindow::_removeWidget(Widget *widget)
{
	if (fWidget == widget)
	{
		fWidget->pData->needsFullViewport = false;
		fWidget = nullptr;
	}
	Window::_removeWidget(widget);
}

// -----------------------------------------------------------------------

END_NAMESPACE_DGL

#undef DBG
#undef DBGF
