/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsIMM32Handler_h__
#define nsIMM32Handler_h__

#include "nscore.h"
#include <windows.h>
#include "nsString.h"
#include "nsTArray.h"
#include "mozilla/EventForwards.h"

class nsIWidget;
class nsWindow;
struct nsIntRect;

namespace mozilla {
namespace widget {

struct MSGResult;

} // namespace widget
} // namespace mozilla

class nsIMEContext
{
public:
  nsIMEContext(HWND aWnd) : mWnd(aWnd)
  {
    mIMC = ::ImmGetContext(mWnd);
  }

  ~nsIMEContext()
  {
    if (mIMC) {
      ::ImmReleaseContext(mWnd, mIMC);
      mIMC = nullptr;
    }
  }

  HIMC get() const
  {
    return mIMC;
  }

  bool IsValid() const
  {
    return !!mIMC;
  }

  void SetOpenState(bool aOpen) const
  {
    if (!mIMC) {
      return;
    }
    ::ImmSetOpenStatus(mIMC, aOpen);
  }

  bool GetOpenState() const
  {
    if (!mIMC) {
      return false;
    }
    return (::ImmGetOpenStatus(mIMC) != FALSE);
  }

  bool AssociateDefaultContext()
  {
    // We assume that there is only default IMC, no new IMC has been created.
    if (mIMC) {
      return false;
    }
    if (!::ImmAssociateContextEx(mWnd, nullptr, IACE_DEFAULT)) {
      return false;
    }
    mIMC = ::ImmGetContext(mWnd);
    return (mIMC != nullptr);
  }

  bool Disassociate()
  {
    if (!mIMC) {
      return false;
    }
    if (!::ImmAssociateContextEx(mWnd, nullptr, 0)) {
      return false;
    }
    ::ImmReleaseContext(mWnd, mIMC);
    mIMC = nullptr;
    return true;
  }

protected:
  nsIMEContext()
  {
    NS_ERROR("Don't create nsIMEContext without window handle");
  }

  nsIMEContext(const nsIMEContext &aSrc) : mWnd(nullptr), mIMC(nullptr)
  {
    NS_ERROR("Don't copy nsIMEContext");
  }

  HWND mWnd;
  HIMC mIMC;
};

class nsIMM32Handler
{
  typedef mozilla::widget::MSGResult MSGResult;
public:
  static void Initialize();
  static void Terminate();

  // If Process*() returns true, the caller shouldn't do anything anymore.
  static bool ProcessMessage(nsWindow* aWindow, UINT msg,
                             WPARAM& wParam, LPARAM& lParam,
                             MSGResult& aResult);
  static bool IsComposing()
  {
    return IsComposingOnOurEditor() || IsComposingOnPlugin();
  }
  static bool IsComposingOn(nsWindow* aWindow)
  {
    return IsComposing() && IsComposingWindow(aWindow);
  }

#ifdef DEBUG
  /**
   * IsIMEAvailable() returns TRUE when current keyboard layout has IME.
   * Otherwise, FALSE.
   */
  static bool IsIMEAvailable() { return !!::ImmIsIME(::GetKeyboardLayout(0)); }
#endif

  // If aForce is TRUE, these methods doesn't check whether we have composition
  // or not.  If you don't set it to TRUE, these method doesn't commit/cancel
  // the composition on uexpected window.
  static void CommitComposition(nsWindow* aWindow, bool aForce = false);
  static void CancelComposition(nsWindow* aWindow, bool aForce = false);

protected:
  static void EnsureHandlerInstance();

  static bool IsComposingOnOurEditor();
  static bool IsComposingOnPlugin();
  static bool IsComposingWindow(nsWindow* aWindow);

  static bool ShouldDrawCompositionStringOurselves();
  static void InitKeyboardLayout(HKL aKeyboardLayout);
  static UINT GetKeyboardCodePage();

  /**
   * Checks whether the window is top level window of the composing window.
   * In this method, the top level window means in all windows, not only in all
   * OUR windows.  I.e., if the aWindow is embedded, this always returns FALSE.
   */
  static bool IsTopLevelWindowOfComposition(nsWindow* aWindow);

  static bool ProcessInputLangChangeMessage(nsWindow* aWindow,
                                              WPARAM wParam,
                                              LPARAM lParam,
                                              MSGResult& aResult);
  static bool ProcessMessageForPlugin(nsWindow* aWindow, UINT msg,
                                        WPARAM &wParam, LPARAM &lParam,
                                        MSGResult& aResult);

  nsIMM32Handler();
  ~nsIMM32Handler();

  // On*() methods return true if the caller of message handler shouldn't do
  // anything anymore.  Otherwise, false.
  bool OnMouseEvent(nsWindow* aWindow, LPARAM lParam, int aAction,
                    MSGResult& aResult);
  static bool OnKeyDownEvent(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                             MSGResult& aResult);

  bool OnIMEStartComposition(nsWindow* aWindow, MSGResult& aResult);
  bool OnIMEStartCompositionOnPlugin(nsWindow* aWindow,
                                     WPARAM wParam, LPARAM lParam,
                                     MSGResult& aResult);
  bool OnIMEComposition(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                        MSGResult& aResult);
  bool OnIMECompositionOnPlugin(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                                MSGResult& aResult);
  bool OnIMEEndComposition(nsWindow* aWindow, MSGResult& aResult);
  bool OnIMEEndCompositionOnPlugin(nsWindow* aWindow, WPARAM wParam,
                                   LPARAM lParam, MSGResult& aResult);
  bool OnIMERequest(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                    MSGResult& aResult);
  bool OnIMECharOnPlugin(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                         MSGResult& aResult);
  bool OnChar(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
              MSGResult& aResult);
  bool OnCharOnPlugin(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                      MSGResult& aResult);
  void OnInputLangChange(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                         MSGResult& aResult);

  // These message handlers don't use instance members, we should not create
  // the instance by the messages.  So, they should be static.
  static bool OnIMEChar(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                        MSGResult& aResult);
  static bool OnIMESetContext(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                              MSGResult& aResult);
  static bool OnIMESetContextOnPlugin(nsWindow* aWindow,
                                      WPARAM wParam, LPARAM lParam,
                                      MSGResult& aResult);
  static bool OnIMECompositionFull(nsWindow* aWindow, MSGResult& aResult);
  static bool OnIMENotify(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                          MSGResult& aResult);
  static bool OnIMESelect(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                          MSGResult& aResult);

  // The result of Handle* method mean "Processed" when it's TRUE.
  void HandleStartComposition(nsWindow* aWindow,
                              const nsIMEContext &aIMEContext);
  bool HandleComposition(nsWindow* aWindow, const nsIMEContext &aIMEContext,
                           LPARAM lParam);
  void HandleEndComposition(nsWindow* aWindow);
  bool HandleReconvert(nsWindow* aWindow, LPARAM lParam, LRESULT *oResult);
  bool HandleQueryCharPosition(nsWindow* aWindow, LPARAM lParam,
                                 LRESULT *oResult);
  bool HandleDocumentFeed(nsWindow* aWindow, LPARAM lParam, LRESULT *oResult);

  /**
   *  When a window's IME context is activating but we have composition on
   *  another window, we should commit our composition because IME context is
   *  shared by all our windows (including plug-ins).
   *  @param aWindow is a new activated window.
   *  If aWindow is our composing window, this method does nothing.
   *  Otherwise, this commits the composition on the previous window.
   *  If this method did commit a composition, this returns TRUE.
   */
  bool CommitCompositionOnPreviousWindow(nsWindow* aWindow);

  /**
   *  ResolveIMECaretPos
   *  Convert the caret rect of a composition event to another widget's
   *  coordinate system.
   *
   *  @param aReferenceWidget The origin widget of aCursorRect.
   *                          Typically, this is mReferenceWidget of the
   *                          composing events. If the aCursorRect is in screen
   *                          coordinates, set nullptr.
   *  @param aCursorRect      The cursor rect.
   *  @param aNewOriginWidget aOutRect will be in this widget's coordinates. If
   *                          this is nullptr, aOutRect will be in screen
   *                          coordinates.
   *  @param aOutRect         The converted cursor rect.
   */
  void ResolveIMECaretPos(nsIWidget* aReferenceWidget,
                          nsIntRect& aCursorRect,
                          nsIWidget* aNewOriginWidget,
                          nsIntRect& aOutRect);

  bool ConvertToANSIString(const nsAFlatString& aStr,
                             UINT aCodePage,
                             nsACString& aANSIStr);

  bool SetIMERelatedWindowsPos(nsWindow* aWindow,
                                 const nsIMEContext &aIMEContext);
  bool GetCharacterRectOfSelectedTextAt(nsWindow* aWindow,
                                          uint32_t aOffset,
                                          nsIntRect &aCharRect);
  bool GetCaretRect(nsWindow* aWindow, nsIntRect &aCaretRect);
  void GetCompositionString(const nsIMEContext &aIMEContext, DWORD aIndex);
  /**
   *  Get the current target clause of composition string.
   *  If there are one or more characters whose attribute is ATTR_TARGET_*,
   *  this returns the first character's offset and its length.
   *  Otherwise, e.g., the all characters are ATTR_INPUT, this returns
   *  the composition string range because the all is the current target.
   *
   *  aLength can be null (default), but aOffset must not be null.
   *
   *  The aOffset value is offset in the contents.  So, when you need offset
   *  in the composition string, you need to subtract mCompositionStart from it.
   */
  bool GetTargetClauseRange(uint32_t *aOffset, uint32_t *aLength = nullptr);
  void DispatchTextEvent(nsWindow* aWindow, const nsIMEContext &aIMEContext,
                         bool aCheckAttr = true);
  void SetTextRangeList(nsTArray<mozilla::TextRange>& aTextRangeList);

  nsresult EnsureClauseArray(int32_t aCount);
  nsresult EnsureAttributeArray(int32_t aCount);

  /**
   * When WM_IME_CHAR is received and passed to DefWindowProc, we need to
   * record the messages.  In other words, we should record the messages
   * when we receive WM_IME_CHAR on windowless plug-in (if we have focus,
   * we always eat them).  When focus is moved from a windowless plug-in to
   * our window during composition, WM_IME_CHAR messages were received when
   * the plug-in has focus.  However, WM_CHAR messages are received after the
   * plug-in lost focus.  So, we need to ignore the WM_CHAR messages because
   * they make unexpected text input events on us.
   */
  nsTArray<MSG> mPassedIMEChar;

  bool IsIMECharRecordsEmpty()
  {
    return mPassedIMEChar.IsEmpty();
  }
  void ResetIMECharRecords()
  {
    mPassedIMEChar.Clear();
  }
  void DequeueIMECharRecords(WPARAM &wParam, LPARAM &lParam)
  {
    MSG msg = mPassedIMEChar.ElementAt(0);
    wParam = msg.wParam;
    lParam = msg.lParam;
    mPassedIMEChar.RemoveElementAt(0);
  }
  void EnqueueIMECharRecords(WPARAM wParam, LPARAM lParam)
  {
    MSG msg;
    msg.wParam = wParam;
    msg.lParam = lParam;
    mPassedIMEChar.AppendElement(msg);
  }

  nsWindow* mComposingWindow;
  nsString  mCompositionString;
  nsString  mLastDispatchedCompositionString;
  InfallibleTArray<uint32_t> mClauseArray;
  InfallibleTArray<uint8_t> mAttributeArray;

  int32_t mCursorPosition;
  uint32_t mCompositionStart;

  bool mIsComposing;
  bool mIsComposingOnPlugin;
  bool mNativeCaretIsCreated;

  static UINT sCodePage;
  static DWORD sIMEProperty;
};

#endif // nsIMM32Handler_h__
