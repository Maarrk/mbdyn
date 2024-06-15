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
from enum import Enum
from typing import Any, Optional, Union

try:
    from pydantic import BaseModel, ConfigDict
    # HACK: this is not public, but does exactly what we need
    from pydantic._internal._docs_extraction import extract_docstrings_from_cls

    class _EntityBase(BaseModel):
        """Configuration for Entity with pydantic available"""
        model_config = ConfigDict(extra='forbid',
                                  use_attribute_docstrings=True)

        @classmethod
        def model_json_schema(cls, *args, **kwargs) -> dict[str, Any]:
            """Changes enum definitions to `anyOf` with a list of `const` values and adds descriptions.

            Needs the enum types to be imported with their name (without prefix)"""
            schema = super().model_json_schema(cls, *args, **kwargs)
            for typename in schema['$defs']:
                if 'enum' in schema['$defs'][typename]:
                    type_var = globals()[typename]
                    docstrings = extract_docstrings_from_cls(type_var, True)
                    values = ['', 'Entries:']
                    for entry in schema['$defs'][typename]['enum']:
                        values.append(
                            f'- {entry}: {docstrings[type_var(entry).name]}'
                        )
                    schema['$defs'][typename]['description'] \
                        += '\n'.join(values)

            # allow specifying the schema in the root object
            schema['properties']['$schema'] = {"type": "string"}

            return schema

except ImportError:
    class _EntityBase:
        """Placeholder with minimal functionality for running a correct model when pydantic isn't available"""

        def __init__(self, *args, **kwargs):
            if len(args) > 0:
                raise TypeError(
                    'MBDyn entities cannot be initialized using positional arguments')
            for key, value in kwargs.items():
                setattr(self, key, value)


class MBEntity(_EntityBase, ABC):
    """Base class for every 'thing' to put in MBDyn file, other than numbers"""

    @abstractmethod
    def __str__(self) -> str:
        """Has to be overridden to output the MBDyn syntax"""
        pass

    @classmethod
    def from_dict(cls, data: dict):
        # TODO: Make this work for nested entities
        return cls(**data)


# HACK: We need to annotate types on enum entries to find descriptions for schema same way as other classes
class MBVarType(str, Enum):
    """Built-in types in math parser"""
    BOOL: str = 'bool'
    """Boolean number (promoted to `integer`, `real`, or `string` (0 or 1), whenever required)"""
    INTEGER: str = 'integer'
    """Integer number (promoted to `real`, or `string`, whenever required)"""
    REAL: str = 'real'
    """Real number (promoted to `string` whenever required)"""
    STRING: str = 'string'
    """Text string"""


class MBVar(MBEntity):
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


class DriveCaller(MBEntity):
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
    idx: Optional[Union[MBVar, int]] = None
    """Index of this drive to reuse with references"""

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


class ConstDriveCaller(DriveCaller):
    """An example of `DriveCaller` that always returns the same constant value"""

    # Note that method docstrings are inherited correctly (unlike dataclass fields)
    def drive_type(self):
        return 'const'

    const_value: Union[MBVar, float, int]
    """Value that will be output by the drive"""

    def __str__(self):
        return f'''{self.drive_header()}, {self.const_value}'''


if __name__ == '__main__':
    # write out the schema
    try:
        import json

        with open('json/schema_const_drive_caller.json', 'w') as out:
            json.dump(ConstDriveCaller.model_json_schema(), out, indent=2)
    except ImportError:
        print('Generating JSON schema depends on pydantic package')
