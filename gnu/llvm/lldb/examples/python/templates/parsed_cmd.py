"""
This module implements a couple of utility classes to make writing
lldb parsed commands more Pythonic.
The way to use it is to make a class for your command that inherits from ParsedCommandBase.
That will make an LLDBOptionValueParser which you will use for your
option definition, and to fetch option values for the current invocation
of your command.  Access to the OV parser is through:

ParsedCommandBase.get_parser()

Next, implement setup_command_definition() in your new command class, and call:

  self.get_parser().add_option()

to add all your options.  The order doesn't matter for options, lldb will sort them
alphabetically for you when it prints help.

Similarly you can define the arguments with:

  self.get_parser().add_argument()

At present, lldb doesn't do as much work as it should verifying arguments, it
only checks that commands that take no arguments don't get passed arguments.

Then implement the execute function for your command as:

    def __call__(self, debugger, args_list, exe_ctx, result):

The arguments will be a list of strings.  

You can access the option values using the 'dest' string you passed in when defining the option.
And if you need to know whether a given option was set by the user or not, you can
use the was_set API.  

So for instance, if you have an option whose "dest" is "my_option", then:

    self.get_parser().my_option

will fetch the value, and:

    self.get_parser().was_set("my_option")

will return True if the user set this option, and False if it was left at its default
value.

There are example commands in the lldb testsuite at:

llvm-project/lldb/test/API/commands/command/script/add/test_commands.py
"""
import inspect
import lldb
import sys
from abc import abstractmethod

# Some methods to translate common value types.  Should return a
# tuple of the value and an error value (True => error) if the
# type can't be converted.  These are called internally when the
# command line is parsed into the 'dest' properties, you should
# not need to call them directly.
# FIXME: Need a way to push the conversion error string back to lldb.
def to_bool(in_value):
    error = True
    value = False
    if type(in_value) != str or len(in_value) == 0:
        return (value, error)

    low_in = in_value.lower()
    if low_in in ["y", "yes", "t", "true", "1"]:
        value = True
        error = False
        
    if not value and low_in in ["n", "no", "f", "false", "0"]:
        value = False
        error = False

    return (value, error)

def to_int(in_value):
    #FIXME: Not doing errors yet...
    return (int(in_value), False)

def to_unsigned(in_value):
    # FIXME: find an unsigned converter...
    # And handle errors.
    return (int(in_value), False)

translators = {
    lldb.eArgTypeBoolean : to_bool,
    lldb.eArgTypeBreakpointID : to_unsigned,
    lldb.eArgTypeByteSize : to_unsigned,
    lldb.eArgTypeCount : to_unsigned,
    lldb.eArgTypeFrameIndex : to_unsigned,
    lldb.eArgTypeIndex : to_unsigned,
    lldb.eArgTypeLineNum : to_unsigned,
    lldb.eArgTypeNumLines : to_unsigned,
    lldb.eArgTypeNumberPerLine : to_unsigned,
    lldb.eArgTypeOffset : to_int,
    lldb.eArgTypeThreadIndex : to_unsigned,
    lldb.eArgTypeUnsignedInteger : to_unsigned,
    lldb.eArgTypeWatchpointID : to_unsigned,
    lldb.eArgTypeColumnNum : to_unsigned,
    lldb.eArgTypeRecognizerID : to_unsigned,
    lldb.eArgTypeTargetID : to_unsigned,
    lldb.eArgTypeStopHookID : to_unsigned
}

def translate_value(value_type, value):
    try:
        return translators[value_type](value)
    except KeyError:
        # If we don't have a translator, return the string value.
        return (value, False)

class LLDBOptionValueParser:
    """
    This class holds the option definitions for the command, and when
    the command is run, you can ask the parser for the current values.  """

    def __init__(self):
        # This is a dictionary of dictionaries.  The key is the long option
        # name, and the value is the rest of the definition.
        self.options_dict = {}
        self.args_array = []


    # FIXME: would this be better done on the C++ side?
    # The common completers are missing some useful ones.
    # For instance there really should be a common Type completer
    # And an "lldb command name" completer.
    completion_table = {
        lldb.eArgTypeAddressOrExpression : lldb.eVariablePathCompletion,
        lldb.eArgTypeArchitecture : lldb.eArchitectureCompletion,
        lldb.eArgTypeBreakpointID : lldb.eBreakpointCompletion,
        lldb.eArgTypeBreakpointIDRange : lldb.eBreakpointCompletion,
        lldb.eArgTypeBreakpointName : lldb.eBreakpointNameCompletion,
        lldb.eArgTypeClassName : lldb.eSymbolCompletion,
        lldb.eArgTypeDirectoryName : lldb.eDiskDirectoryCompletion,
        lldb.eArgTypeExpression : lldb.eVariablePathCompletion,
        lldb.eArgTypeExpressionPath : lldb.eVariablePathCompletion,
        lldb.eArgTypeFilename : lldb.eDiskFileCompletion,
        lldb.eArgTypeFrameIndex : lldb.eFrameIndexCompletion,
        lldb.eArgTypeFunctionName : lldb.eSymbolCompletion,
        lldb.eArgTypeFunctionOrSymbol : lldb.eSymbolCompletion,
        lldb.eArgTypeLanguage : lldb.eTypeLanguageCompletion,
        lldb.eArgTypePath : lldb.eDiskFileCompletion,
        lldb.eArgTypePid : lldb.eProcessIDCompletion,
        lldb.eArgTypeProcessName : lldb.eProcessNameCompletion,
        lldb.eArgTypeRegisterName : lldb.eRegisterCompletion,
        lldb.eArgTypeRunArgs : lldb.eDiskFileCompletion,
        lldb.eArgTypeShlibName : lldb.eModuleCompletion,
        lldb.eArgTypeSourceFile : lldb.eSourceFileCompletion,
        lldb.eArgTypeSymbol : lldb.eSymbolCompletion,
        lldb.eArgTypeThreadIndex : lldb.eThreadIndexCompletion,
        lldb.eArgTypeVarName : lldb.eVariablePathCompletion,
        lldb.eArgTypePlatform : lldb.ePlatformPluginCompletion,
        lldb.eArgTypeWatchpointID : lldb.eWatchpointIDCompletion,
        lldb.eArgTypeWatchpointIDRange : lldb.eWatchpointIDCompletion,
        lldb.eArgTypeModuleUUID : lldb.eModuleUUIDCompletion,
        lldb.eArgTypeStopHookID : lldb.eStopHookIDCompletion
    }

    @classmethod
    def determine_completion(cls, arg_type):
        return cls.completion_table.get(arg_type, lldb.eNoCompletion)

    def add_argument_set(self, arguments):
        self.args_array.append(arguments)

    def get_option_element(self, long_name):
        return self.options_dict.get(long_name, None)

    def is_enum_opt(self, opt_name):
        elem = self.get_option_element(opt_name)
        if not elem:
            return False
        return "enum_values" in elem

    def option_parsing_started(self):
        """ This makes the ivars for all the "dest" values in the array and gives them
            their default values.  You should not have to call this by hand, though if
            you have some option that needs to do some work when a new command invocation
            starts, you can override this to handle your special option.  """
        for key, elem in self.options_dict.items():
            elem['_value_set'] = False
            try:
                object.__setattr__(self, elem["dest"], elem["default"])
            except AttributeError:
                # It isn't an error not to have a "dest" variable name, you'll
                # just have to manage this option's value on your own.
                continue

    def set_enum_value(self, enum_values, input):
        """ This sets the value for an enum option, you should not have to call this
        by hand.  """
        candidates = []
        for candidate in enum_values:
            # The enum_values are a two element list of value & help string.
            value = candidate[0]
            if value.startswith(input):
                candidates.append(value)

        if len(candidates) == 1:
            return (candidates[0], False)
        else:
            return (input, True)
        
    def set_option_value(self, exe_ctx, opt_name, opt_value):
        """ This sets a single option value.  This will handle most option
        value types, but if you have an option that has some complex behavior,
        you can override this to implement that behavior, and then pass the
        rest of the options to the base class implementation. """
        elem = self.get_option_element(opt_name)
        if not elem:
            return False
        
        if "enum_values" in elem:
            (value, error) = self.set_enum_value(elem["enum_values"], opt_value)
        else:
            (value, error)  = translate_value(elem["value_type"], opt_value)

        if error:
            return False
        
        object.__setattr__(self, elem["dest"], value)
        elem["_value_set"] = True
        return True

    def was_set(self, opt_name):
        """ Call this in the __call__ method of your command to determine
            whether this option was set on the command line.  It is sometimes
            useful to know whether an option has the default value because the
            user set it explicitly (was_set -> True) or not.  """

        elem = self.get_option_element(opt_name)
        if not elem:
            return False
        try:
            return elem["_value_set"]
        except AttributeError:
            return False

    def add_option(self, short_option, long_option, help, default,
                   dest = None, required=False, groups = None,
                   value_type=lldb.eArgTypeNone, completion_type=None,
                   enum_values=None):
        """
        short_option: one character, must be unique, not required
        long_option: no spaces, must be unique, required
        help: a usage string for this option, will print in the command help
        default: the initial value for this option (if it has a value)
        dest: the name of the property that gives you access to the value for
                 this value.  Defaults to the long option if not provided.
        required: if true, this option must be provided or the command will error out
        groups: Which "option groups" does this option belong to
        value_type: one of the lldb.eArgType enum values.  Some of the common arg
                    types also have default completers, which will be applied automatically.
        completion_type: currently these are values form the lldb.CompletionType enum, I
                         haven't done custom completions yet.
        enum_values: An array of duples: ["element_name", "element_help"].  If provided,
                     only one of the enum elements is allowed.  The value will be the 
                     element_name for the chosen enum element as a string. 
        """
        if not dest:
            dest = long_option

        if not completion_type:
            completion_type = self.determine_completion(value_type)
            
        dict = {"short_option" : short_option,
                "required" : required,
                "help" : help,
                "value_type" : value_type,
                "completion_type" : completion_type,
                "dest" : dest,
                "default" : default}

        if enum_values:
            dict["enum_values"] = enum_values
        if groups:
            dict["groups"] = groups

        self.options_dict[long_option] = dict

    def make_argument_element(self, arg_type, repeat = "optional", groups = None):
        element = {"arg_type" : arg_type, "repeat" : repeat}
        if groups:
            element["groups"] = groups
        return element

class ParsedCommand:
    def __init__(self, debugger, unused):
        self.debugger = debugger
        self.ov_parser = LLDBOptionValueParser()
        self.setup_command_definition()
        
    def get_options_definition(self):
        return self.get_parser().options_dict

    def get_flags(self):
        return 0

    def get_args_definition(self):
        return self.get_parser().args_array

    # The base class will handle calling these methods
    # when appropriate.
    
    def option_parsing_started(self):
        self.get_parser().option_parsing_started()

    def set_option_value(self, exe_ctx, opt_name, opt_value):
        return self.get_parser().set_option_value(exe_ctx, opt_name, opt_value)

    def get_parser(self):
        """Returns the option value parser for this command.
        When defining the command, use the parser to add
        argument and option definitions to the command.
        When you are in the command callback, the parser
        gives you access to the options passes to this
        invocation"""

        return self.ov_parser

    # These are the two "pure virtual" methods:
    @abstractmethod
    def __call__(self, debugger, args_array, exe_ctx, result):
        """This is the command callback.  The option values are
        provided by the 'dest' properties on the parser.
    
        args_array: This is the list of arguments provided.
        exe_ctx: Gives the SBExecutionContext on which the
                 command should operate.
        result:  Any results of the command should be
                 written into this SBCommandReturnObject.
        """
        raise NotImplementedError()

    @abstractmethod
    def setup_command_definition(self):
        """This will be called when your command is added to
        the command interpreter.  Here is where you add your
        options and argument definitions for the command."""
        raise NotImplementedError()

    @staticmethod
    def do_register_cmd(cls, debugger, module_name):
        """ Add any commands contained in this module to LLDB """
        command = "command script add -o -p -c %s.%s %s" % (
            module_name,
            cls.__name__,
            cls.program,
        )
        debugger.HandleCommand(command)
        print(
            'The "{0}" command has been installed, type "help {0}"'
            'for detailed help.'.format(cls.program)
        )
