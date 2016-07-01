/*
    This file is part of the WebKit open source project.
    This file has been generated by generate-bindings.pl. DO NOT MODIFY!

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"

#if ENABLE(SQL_DATABASE)

#include "JSTestCallback.h"

#include "JSClass1.h"
#include "JSClass2.h"
#include "JSDOMStringList.h"
#include "ScriptExecutionContext.h"
#include <runtime/JSLock.h>

using namespace JSC;

namespace WebCore {

JSTestCallback::JSTestCallback(JSObject* callback, JSDOMGlobalObject* globalObject)
    : ActiveDOMCallback(globalObject->scriptExecutionContext())
    , m_data(new JSCallbackData(callback, globalObject))
{
}

JSTestCallback::~JSTestCallback()
{
    ScriptExecutionContext* context = scriptExecutionContext();
    // When the context is destroyed, all tasks with a reference to a callback
    // should be deleted. So if the context is 0, we are on the context thread.
    if (!context || context->isContextThread())
        delete m_data;
    else
        context->postTask(DeleteCallbackDataTask::create(m_data));
#ifndef NDEBUG
    m_data = 0;
#endif
}

// Functions

bool JSTestCallback::callbackWithNoParam()
{
    if (!canInvokeCallback())
        return true;

    RefPtr<JSTestCallback> protect(this);

    JSLock lock(SilenceAssertionsOnly);

    ExecState* exec = m_data->globalObject()->globalExec();
    MarkedArgumentBuffer args;

    bool raisedException = false;
    m_data->invokeCallback(args, &raisedException);
    return !raisedException;
}

bool JSTestCallback::callbackWithClass1Param(Class1* class1Param)
{
    if (!canInvokeCallback())
        return true;

    RefPtr<JSTestCallback> protect(this);

    JSLock lock(SilenceAssertionsOnly);

    ExecState* exec = m_data->globalObject()->globalExec();
    MarkedArgumentBuffer args;
    args.append(toJS(exec, m_data->globalObject(), class1Param));

    bool raisedException = false;
    m_data->invokeCallback(args, &raisedException);
    return !raisedException;
}

bool JSTestCallback::callbackWithClass2Param(Class2* class2Param, const String& strArg)
{
    if (!canInvokeCallback())
        return true;

    RefPtr<JSTestCallback> protect(this);

    JSLock lock(SilenceAssertionsOnly);

    ExecState* exec = m_data->globalObject()->globalExec();
    MarkedArgumentBuffer args;
    args.append(toJS(exec, m_data->globalObject(), class2Param));
    args.append(jsString(exec, strArg));

    bool raisedException = false;
    m_data->invokeCallback(args, &raisedException);
    return !raisedException;
}

bool JSTestCallback::callbackWithStringList(DOMStringList* listParam)
{
    if (!canInvokeCallback())
        return true;

    RefPtr<JSTestCallback> protect(this);

    JSLock lock(SilenceAssertionsOnly);

    ExecState* exec = m_data->globalObject()->globalExec();
    MarkedArgumentBuffer args;
    args.append(toJS(exec, m_data->globalObject(), listParam));

    bool raisedException = false;
    m_data->invokeCallback(args, &raisedException);
    return !raisedException;
}

}

#endif // ENABLE(SQL_DATABASE)
