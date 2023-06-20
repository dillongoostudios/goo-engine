# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>

"""
Custom file version warning system, to avoid mistakes with regular blender builds and production files.
"""

import bpy
from bpy.types import Operator
from bpy.app.handlers import persistent
import os

script_name = ".version_warning.py"
script_content = """
import bpy

def draw_popup_warning(self, context):
    self.layout.label(text=f"Warning: file edited in Goo Engine, data may be lost if used in standard Blender!")

if not bpy.app.version_string.endswith('Goo Engine'):
    print("File loaded, not a Goo Engine build!")
    bpy.context.window_manager.popup_menu(draw_popup_warning, title="Warning", icon='ERROR')
"""


@persistent
def save_handler(_):
    if not bpy.context.preferences.filepaths.save_version_warning:
        return
    text = bpy.data.texts.get(script_name)
    if not text:
        text = bpy.data.texts.new(script_name)
    text.use_module = True
    text.clear()
    text.write(script_content)

def register():
    bpy.app.handlers.save_pre.append(save_handler)

def unregister():
    bpy.app.handlers.save_pre.remove(save_handler)


if __name__ == "__main__":
    register()