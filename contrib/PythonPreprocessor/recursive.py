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
from numbers import Number, Integral
from typing import Optional, Union


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


class MBVar(_MBEntity):
    pass


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
    # FIXME: Can't assign defaults here if subclasses have required arguments
    idx: Optional[Union[MBVar, Integral]]
    """Index of this drive to reuse with references, possibly not assigned"""
    drive_type: str

    def drive_header(self) -> str:
        """common syntax for start of any drive caller"""
        # it's not just `__str__` to still require overriding it in specific drives
        if self.idx is not None:
            # The idx possibly being None communicates the intent more clearly than checking if it's >=0
            return f'drive caller: {self.idx}, {self.drive_type}'
        else:
            return self.drive_type


# FIXME: When using dataclass, there are no hints when typing the constructor
@dataclass
class ConstDriveCaller(DriveCaller):
    """An example of `DriveCaller` that always returns the same constant value"""
    drive_type = 'const'
    const_value: Union[Number, MBVar]
    """Value that will be output by the drive"""
    # FIXME: Would prefer to assign the default in the parent class, also requires copying the doc comment
    idx: Optional[Union[MBVar, Integral]] = None
    """Index of this drive to reuse with references"""

    def __str__(self):
        return f'''{self.drive_header()}, {self.const_value}'''
