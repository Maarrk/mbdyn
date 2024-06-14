"""
This module is illustrating a proposed approach to rework MBDynLib

Objectives:
- Reuse validation and printing logic for object types
- Don't write repetitive type validation code and error messages by hand
- Improve editor support:
  - Type hints
  - Allowed values
  - Inline documentation
- Allow loading from dictionaries if they match the structure
  - Load JSON, YAML, TOML and other formats this way
  - "to allow more liberal definition of nodes" (Merge Request 42)
- Attempting to support Python 3.6, but no newer than Python 3.8
  - Type annotations were introduced in 3.6, f-strings as well
  - dataclasses were introduced in 3.7
  - typing.Literal was introduced in 3.8
  - 3.8 is last supported version as of writing, packages need that
- Must run the model without any external packages, worse editing experience is acceptable
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass
from enum import Enum
from typing import Optional, Union

try:
    import pydantic.dataclasses

    def forbidden_extra(*args, **kwargs):
        """Set extra fields to forbidden and call normal dataclass decorator"""
        new_config = {'extra': 'forbid'}
        config = dict()
        if 'config' in kwargs and isinstance(kwargs['config'], dict):
            config = kwargs['config'].update(new_config)
        else:
            config = new_config

        if 'config' in kwargs:
            del kwargs['config']
        return pydantic.dataclasses.dataclass(*args, **kwargs, config=config)

    # Set the code to use this modified dataclass
    # HACK: hidden from code analysis in exec to keep the default type hints
    exec('dataclass = forbidden_extra')
except ImportError:
    pass


@dataclass
class _MBEntity(ABC):
    """Base class for every 'thing' to put in MBDyn file, other than numbers"""

    @abstractmethod
    def __str__(self) -> str:
        """Has to be overridden to output the MBDyn syntax"""
        pass

    @classmethod
    def from_dict(cls, data: dict):
        # TODO: Make this work for nested entities
        return cls(**data)


class MBVarType(str, Enum):
    """Built-in types in math parser"""
    BOOL = 'bool'
    """Boolean number (promoted to `integer`, `real`,or `string` (0 or 1), whenever required)"""
    INTEGER = 'integer'
    """Integer number (promoted to `real`, or `string`, whenever required)"""
    REAL = 'real'
    """Real number (promoted to `string` whenever required)"""
    STRING = 'string'
    """Text string"""


@dataclass
class MBVar(_MBEntity):
    """
    Every time a numeric value is expected, the result of evaluating
    a mathematical expression can be used, including variable declaration
    and assignment (variable names and values are kept in memory throughout
    the input phase and the simulation) and simple math functions.
    Limited math on strings is also supported.
    Named variables and non-named constants are strongly typed.
    """
    name: str
    """Name of the variable"""
    var_type: MBVarType
    """Type as defined in MBDyn"""
    expression: str
    """For now just a string, TODO: finish implementing expressions and MBVar"""

    def __str__(self) -> str:
        return self.name


def redefine_default(func):
    '''
    TL;DR: When inheriting this class, redefine it as a field

    ```python
    # example:
    class Parent:
        @redefine_default
        def foo(self) -> FieldType:
            """Shared member for all `Parent`"""
            return bar

    # you're expected to copy it like this:
    @dataclass
    class Child(Parent):
        baz: OtherType
        """Required property specific to `Child`"""
        foo: FieldType = bar
        """Shared member for all`Parent`"""
    ```

    A function with this decorator should be redefined as dataclass property in every child class.
    In here it returns the expected default, and is defined with a documentation string.

    This creates some duplication, but a redefinition is required to reorder members with defaults,
    and gives correct type hints and docstring everywhere it's used. Modifying the `dataclass` decorator
    doesn't help because of special treatment and assumptions from code analysis tools.
    It is a known limitation, which in Python 3.10 is solved by adding the `kw_only` option to dataclass,
    however this would make the library code simpler, and user code more verbose, which is not a goal.

    Marking this as `abstractmethod` enforces it to be defined, and `property` shows that it should be
    accessed like a field here (which it will in fact be in the derived classes)
    '''
    return property(abstractmethod(func))


@dataclass
class DriveCaller(_MBEntity):
    """
    Abstract class for C++ type `DriveCaller`. Every time some entity can be driven, i.e. a value can be expressed
    as dependent on some external input, an object of the class  `DriveCaller` is used.

    The `drive` essentially represents a scalar function, whose value can change over time or,
    through some more sophisticated means, can depend on the state of the analysis.
    Usually, the dependence over time is implicitly assumed, unless otherwise specified.

    For example, the amplitude of the force applied by a  `force` element is defined by means of a `drive`;
    as such, the value of the `drive` is implicitly calculated as a function of the time.
    However, a  `dof drive` uses a subordinate `drive` to compute its value based on the value of
    a degree of freedom of the analysis; as a consequence, the value of the `dof drive` is represented
    by the value of the subordinate `drive` when evaluated as a function of that specific degree of freedom
    at the desired time (function of function).

    The family of the `DriveCaller` object is very large.
    """
    @redefine_default
    def idx(self) -> Optional[Union[MBVar, int]]:
        """Index of this drive to reuse with references"""
        return None

    @abstractmethod
    def drive_type(self) -> str:
        """Every drive class must define this to return its MBDyn syntax name"""
        raise NotImplementedError("called drive_type of abstract DriveCaller")

    def drive_header(self) -> str:
        """common syntax for start of any drive caller"""
        # it's not just `__str__` to still require overriding it in specific drives
        if self.idx is not None:
            # The idx possibly being None communicates the intent more clearly than checking if it's >=0
            return f'drive caller: {self.idx}, {self.drive_type()}'
        else:
            return self.drive_type()


@dataclass
class ConstDriveCaller(DriveCaller):
    """An example of `DriveCaller` that always returns the same constant value"""

    # Note that method docstrings are inherited correctly (unlike dataclass fields)
    def drive_type(self):
        return 'const'

    const_value: Union[MBVar, float, int]
    """Value that will be output by the drive"""
    idx: Optional[Union[MBVar, int]] = None
    """Index of this drive to reuse with references"""

    def __str__(self):
        return f'''{self.drive_header()}, {self.const_value}'''


if __name__ == '__main__':
    # write out the schema
    try:
        import json
        from pydantic import TypeAdapter

        with open('json/schema_const_drive_caller.json', 'w') as out:
            # FIXME: dataclass fields don't get the descriptions like BaseModel fields do
            json.dump(TypeAdapter(ConstDriveCaller).json_schema(), out, indent=2)
    except ImportError:
        print('Generating JSON schema depends on pydantic package')
