package com.droiddeck.launcher.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Context.INPUT_METHOD_SERVICE
import android.graphics.Color
import android.util.AttributeSet
import android.util.Log
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.TextView
import com.droiddeck.launcher.R
import com.droiddeck.launcher.session.SessionService
import com.droiddeck.launcher.session.SessionTerminal
import com.termux.terminal.TerminalEmulator
import com.termux.terminal.TerminalSession
import com.termux.terminal.TerminalSessionClient
import com.termux.view.TerminalView
import com.termux.view.TerminalViewClient

/**
 * The Linux terminal as a plain view, so it is not tied to a [android.app.Presentation].
 *
 * It used to exist only inside SecondScreenPresentation, which Android can only
 * show on an external display - on a tablet with no second screen there was no
 * way to reach a shell, which makes debugging a session nearly impossible.
 * The panel is self-contained (title bar, modifier keys, terminal view and its
 * client) and can be dropped into a presentation or a dialog on the main screen.
 */
class TerminalPanel(
    context: Context,
    attrs: AttributeSet? = null,
    private val onClose: () -> Unit,
) : LinearLayout(context, attrs), TerminalSessionClient, TerminalViewClient {

    private var terminalSession: TerminalSession? = null
    private var terminalView: TerminalView? = null
    private var terminalPid = -1
    private var ctrlActive = false
    private var altActive = false

    init {
        orientation = VERTICAL
        setBackgroundColor(Color.rgb(15, 18, 22))
        isFocusableInTouchMode = true
    }

    /** Build the terminal and attach it. Call once, after the view is in a window. */
    fun start() {
        removeAllViews()
        val session = SessionTerminal.create(this)
        if (session == null) {
            addView(titleBar())
            addView(TextView(context).apply {
                text = context.getString(R.string.second_terminal_not_ready)
                setTextColor(Color.WHITE)
                gravity = Gravity.CENTER
            }, LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
            return
        }
        terminalSession = session
        val view = TerminalView(context, null).apply {
            setTerminalViewClient(this@TerminalPanel)
            setTextSize(dp(14))
            isFocusable = true
            isFocusableInTouchMode = true
        }
        terminalView = view
        addView(titleBar())
        addView(keyStrip())
        addView(view, LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        view.attachSession(session)
        view.post {
            registerTerminalPid()
            view.requestFocus()
            showKeyboard()
        }
    }

    /** Tear the session down. Safe to call more than once. */
    fun stop() {
        terminalView?.let { view ->
            (context.getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager)
                .hideSoftInputFromWindow(view.windowToken, 0)
        }
        val pid = terminalPid.takeIf { it > 1 } ?: (terminalSession?.pid ?: -1)
        if (pid > 1) SessionService.stopTerminalProcess(context, pid)
        terminalPid = -1
        terminalSession = null
        terminalView = null
        ctrlActive = false
        altActive = false
        removeAllViews()
    }

    private fun titleBar(): LinearLayout = LinearLayout(context).apply {
        orientation = HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        setPadding(dp(8), dp(4), dp(8), dp(4))
        setBackgroundColor(Color.rgb(27, 32, 38))
        addView(Button(context).apply {
            text = "×"
            contentDescription = context.getString(R.string.deck_close)
            minWidth = dp(48)
            setOnClickListener { onClose() }
        })
        addView(TextView(context).apply {
            text = context.getString(R.string.second_terminal)
            textSize = 14f
            setTextColor(Color.WHITE)
        }, LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        addView(Button(context).apply {
            text = context.getString(R.string.drawer_keyboard)
            setOnClickListener { showKeyboard() }
        })
    }

    private fun keyStrip(): HorizontalScrollView {
        val strip = HorizontalScrollView(context).apply { isHorizontalScrollBarEnabled = false }
        val keys = LinearLayout(context).apply {
            orientation = HORIZONTAL
            setPadding(dp(4), dp(2), dp(4), dp(2))
        }
        fun key(label: String, input: String, onClick: ((Button) -> Unit)? = null) {
            keys.addView(Button(context).apply {
                text = label
                setOnClickListener {
                    if (onClick != null) onClick(this)
                    else terminalSession?.write(input)
                }
            })
        }
        val ctrl = context.getString(R.string.pckb_key_ctrl)
        val alt = context.getString(R.string.pckb_key_alt)
        key(context.getString(R.string.pckb_key_esc), "\u001b")
        key(context.getString(R.string.second_key_tab), "\t")
        key(ctrl, "") { button ->
            ctrlActive = !ctrlActive
            button.text = if (ctrlActive) context.getString(R.string.second_key_on, ctrl) else ctrl
        }
        key(alt, "") { button ->
            altActive = !altActive
            button.text = if (altActive) context.getString(R.string.second_key_on, alt) else alt
        }
        key("↑", "\u001b[A")
        key("↓", "\u001b[B")
        key("←", "\u001b[D")
        key("→", "\u001b[C")
        key(context.getString(R.string.second_key_ctrl_c), "\u0003")
        key(context.getString(R.string.second_key_ctrl_d), "\u0004")
        strip.addView(keys)
        return strip
    }

    private fun showKeyboard() {
        terminalView?.let { view ->
            view.requestFocus()
            (context.getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager)
                .showSoftInput(view, InputMethodManager.SHOW_IMPLICIT)
        }
    }

    private fun registerTerminalPid() {
        val pid = terminalSession?.pid ?: -1
        if (pid > 1 && pid != terminalPid) {
            terminalPid = pid
            SessionService.registerTerminalProcess(context, pid)
        }
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density + 0.5f).toInt()

    // ------------------------------------------------------------ session client

    override fun onTextChanged(changedSession: TerminalSession) {
        terminalView?.onScreenUpdated()
    }

    override fun onTitleChanged(changedSession: TerminalSession) = Unit

    override fun onSessionFinished(finishedSession: TerminalSession) {
        val pid = terminalPid.takeIf { it > 1 } ?: finishedSession.pid
        if (pid > 1) SessionService.terminalProcessExited(context, pid)
        terminalPid = -1
        terminalView?.onScreenUpdated()
    }

    override fun onCopyTextToClipboard(session: TerminalSession, text: String) {
        (context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager)
            .setPrimaryClip(ClipData.newPlainText("Terminal", text))
    }

    override fun onPasteTextFromClipboard(session: TerminalSession) {
        val clip = (context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager).primaryClip
        if (clip != null && clip.itemCount > 0) session.write(clip.getItemAt(0).coerceToText(context).toString())
    }

    override fun onBell(session: TerminalSession) {
        terminalView?.performHapticFeedback(android.view.HapticFeedbackConstants.KEYBOARD_TAP)
    }

    override fun onColorsChanged(changedSession: TerminalSession) {
        terminalView?.invalidate()
    }

    override fun onTerminalCursorStateChange(state: Boolean) {
        terminalView?.setTerminalCursorBlinkerRate(if (state) 600 else 0)
    }

    override fun getTerminalCursorStyle(): Int = TerminalEmulator.TERMINAL_CURSOR_STYLE_BLOCK

    override fun logError(tag: String, message: String) { Log.e(tag, message) }
    override fun logWarn(tag: String, message: String) { Log.w(tag, message) }
    override fun logInfo(tag: String, message: String) { Log.i(tag, message) }
    override fun logDebug(tag: String, message: String) { Log.d(tag, message) }
    override fun logVerbose(tag: String, message: String) { Log.v(tag, message) }
    override fun logStackTraceWithMessage(tag: String, message: String, e: Exception) { Log.e(tag, message, e) }
    override fun logStackTrace(tag: String, e: Exception) { Log.e(tag, e.message ?: "stack trace", e) }

    // -------------------------------------------------------------- view client

    override fun onScale(scale: Float): Float = 1f
    override fun onSingleTapUp(e: MotionEvent) = showKeyboard()
    override fun shouldBackButtonBeMappedToEscape(): Boolean = true
    override fun shouldEnforceCharBasedInput(): Boolean = false
    override fun shouldUseCtrlSpaceWorkaround(): Boolean = false
    override fun isTerminalViewSelected(): Boolean = true
    override fun copyModeChanged(copyMode: Boolean) = Unit
    override fun onKeyDown(keyCode: Int, e: KeyEvent, session: TerminalSession): Boolean = false
    override fun onKeyUp(keyCode: Int, e: KeyEvent): Boolean = false
    override fun onLongPress(event: MotionEvent): Boolean = false
    override fun readControlKey(): Boolean = ctrlActive
    override fun readAltKey(): Boolean = altActive
    override fun readShiftKey(): Boolean = false
    override fun readFnKey(): Boolean = false
    override fun onCodePoint(codePoint: Int, ctrlDown: Boolean, session: TerminalSession): Boolean = false
    override fun onEmulatorSet() {
        terminalView?.onScreenUpdated()
        registerTerminalPid()
    }
}
