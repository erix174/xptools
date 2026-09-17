"""Path discovery for the WEDLivery data scripts.

Every script in this folder used to carry an absolute path into one developer's
machine. That is the same class of mistake as the hardcoded kFlagAssetRoot we
had to fix in the shipped code: it works for exactly one person, and it fails
silently for everyone else.

The rule these scripts follow now:

  * The REPOSITORY is found from this file's own location. A script living in
    tools/scripts/airline_research/ always knows where src/WEDLivery/ is.
  * The X-PLANE ROOT is never guessed. It comes from argv or the XPLANE_ROOT
    environment variable, and is validated the same way WED validates it -
    by checking that the application, Resources/ and Custom Scenery/ are
    siblings, never by folder name.

Nothing here may contain a drive letter or a home directory.
"""

import os
import sys


def repo_root():
    """The xptools checkout this script lives in."""
    here = os.path.dirname(os.path.abspath(__file__))
    # tools/scripts/airline_research -> up three
    return os.path.normpath(os.path.join(here, "..", "..", ".."))


def wed_livery_dir():
    """src/WEDLivery, where the shipped .txt data files live."""
    return os.path.join(repo_root(), "src", "WEDLivery")


def _looks_like_xplane_root(path):
    """Same test WED uses: the application, Resources and Custom Scenery as
    siblings. Deliberately never the folder's NAME - installs get renamed, and a
    nested "X-Plane 12/X-Plane 12/" would pass a name test while being wrong."""
    if not path or not os.path.isdir(path):
        return False
    apps = ("X-Plane.exe", "X-Plane.app", "X-Plane-x86_64", "X-Plane")
    if not any(os.path.exists(os.path.join(path, a)) for a in apps):
        return False
    return (os.path.isdir(os.path.join(path, "Resources")) and
            os.path.isdir(os.path.join(path, "Custom Scenery")))


def xplane_root(argv_index=1):
    """X-Plane root from argv[argv_index], else $XPLANE_ROOT. Exits with a
    usage message rather than guessing - a script that silently reads the wrong
    installation produces an index that looks fine and is wrong, which is worse
    than not running at all."""
    cand = None
    if len(sys.argv) > argv_index:
        cand = sys.argv[argv_index]
    elif os.environ.get("XPLANE_ROOT"):
        cand = os.environ["XPLANE_ROOT"]

    if not cand:
        sys.exit(
            "usage: %s <X-Plane root>\n"
            "   or: set XPLANE_ROOT and run with no arguments\n\n"
            "The root is the folder containing the X-Plane application,\n"
            "Resources/ and Custom Scenery/ side by side.\n"
            % os.path.basename(sys.argv[0]))

    cand = os.path.normpath(cand)
    if not _looks_like_xplane_root(cand):
        sys.exit(
            "'%s' is not an X-Plane root.\n"
            "Expected the application, Resources/ and Custom Scenery/ in it.\n"
            % cand)
    return cand


def default_scenery(root):
    return os.path.join(root, "Resources", "default scenery")


def apt_aircraft(root):
    return os.path.join(default_scenery(root), "sim objects", "apt_aircraft")
