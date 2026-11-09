import pytest

import octalgames


@pytest.fixture(autouse=True)
def _doctest_namespace(doctest_namespace):
    """Make `og` available in docstring examples."""
    doctest_namespace["og"] = octalgames
