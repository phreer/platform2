// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package adaptor

import (
	"chromiumos/dbusbindings/introspect"
	"fmt"
	"strings"

	"chromiumos/dbusbindings/dbustype"
	"chromiumos/dbusbindings/generate/genutil"
)

func makeMethodRetType(method introspect.Method) (string, error) {
	switch method.Kind() {
	case introspect.MethodKindSimple:
		if outputArguments := method.OutputArguments(); len(outputArguments) == 1 {
			baseType, err := outputArguments[0].BaseType(dbustype.DirectionAppend)
			if err != nil {
				return "", err
			}
			return baseType, nil
		}
	case introspect.MethodKindNormal:
		return "bool", nil
	}
	return "void", nil
}

func makeMethodArgs(method introspect.Method) ([]string, error) {
	var methodParams []string
	inputArguments := method.InputArguments()
	outputArguments := method.OutputArguments()

	switch method.Kind() {
	case introspect.MethodKindSimple:
		if len(outputArguments) == 1 {
			outputArguments = nil
			// As we can see from makeMethodRetType function,
			// the only out argument is treated as a normal return value.
		}
	case introspect.MethodKindNormal:
		methodParams = append(methodParams, "brillo::ErrorPtr* error")
		if method.IncludeDBusMessage() {
			methodParams = append(methodParams, "dbus::Message* message")
		}
	case introspect.MethodKindAsync:
		var outTypes []string
		for _, arg := range outputArguments {
			baseType, err := arg.BaseType(dbustype.DirectionAppend)
			if err != nil {
				return nil, err
			}
			outTypes = append(outTypes, baseType)
		}
		param := fmt.Sprintf(
			"std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<%s>> response",
			strings.Join(outTypes, ", "))
		methodParams = append(methodParams, param)
		if method.IncludeDBusMessage() {
			methodParams = append(methodParams, "dbus::Message* message")
		}
		outputArguments = nil
	case introspect.MethodKindRaw:
		methodParams = append(methodParams, "dbus::MethodCall* method_call")
		methodParams = append(methodParams, "brillo::dbus_utils::ResponseSender sender")
		// Raw methods don't take static parameters or return values directly.
		inputArguments = nil
		outputArguments = nil
	}

	index := 1
	for _, c := range []struct {
		args        []introspect.MethodArg
		makeArgType func(*introspect.MethodArg, dbustype.Receiver) (string, error)
		prefix      string
	}{
		{inputArguments, (*introspect.MethodArg).InArgType, "in"},
		{outputArguments, (*introspect.MethodArg).OutArgType, "out"},
	} {
		for _, arg := range c.args {
			paramType, err := c.makeArgType(&arg, dbustype.ReceiverAdaptor)
			if err != nil {
				return nil, err
			}
			paramName := genutil.ArgName(c.prefix, arg.Name, index)
			index++
			methodParams = append(methodParams, fmt.Sprintf("%s %s", paramType, paramName))
		}
	}

	return methodParams, nil
}
