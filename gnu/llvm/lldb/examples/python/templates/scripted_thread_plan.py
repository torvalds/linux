from abc import abstractmethod

import lldb


class ScriptedThreadPlan:
    """
    Class that provides data for an instance of a LLDB 'ScriptedThreadPlan' plug-in class used to construct custom stepping logic.

    """

    def __init__(self, thread_plan: lldb.SBThreadPlan):
        """Initialization needs a valid lldb.SBThreadPlan object. This plug-in will get created after a live process is valid and has stopped.

        Args:
            thread_plan (lldb.SBThreadPlan): The underlying `ThreadPlan` that is pushed onto the plan stack.
        """
        self.thread_plan = thread_plan

    def explains_stop(self, event: lldb.SBEvent) -> bool:
        """Each plan is asked from youngest to oldest if it "explains" the stop. The first plan to claim the stop wins.

        Args:
            event (lldb.SBEvent): The process stop event.

        Returns:
            bool: `True` if this stop could be claimed by this thread plan, `False` otherwise.
            Defaults to `True`.
        """
        return True

    def is_stale(self) -> bool:
        """If your plan is no longer relevant (for instance, you were stepping in a particular stack frame, but some other operation pushed that frame off the stack) return True and your plan will get popped.

        Returns:
            bool: `True` if this thread plan is stale, `False` otherwise.
            Defaults to `False`.
        """
        return False

    def should_stop(self, event: lldb.SBEvent) -> bool:
        """Whether this thread plan should stop and return control to the user.
        If your plan is done at this point, call SetPlanComplete on your thread plan instance. Also, do any work you need here to set up the next stage of stepping.

        Args:
            event (lldb.SBEvent): The process stop event.

        Returns:
            bool: `True` if this plan wants to stop and return control to the user at this point, `False` otherwise.
            Defaults to `False`.
        """
        self.thread_plan.SetPlanComplete(True)
        return True

    def should_step(self) -> bool:
        """Whether this thread plan should instruction step one instruction, or continue till the next breakpoint is hit.

        Returns:
            bool: `True` if this plan will instruction step one instruction, `False` otherwise.
            Defaults to `True`.
        """
        return True

    def stop_description(self, stream: lldb.SBStream) -> None:
        """Customize the thread plan stop reason when the thread plan is complete.

        Args:
            stream (lldb.SBStream): The stream containing the stop description.
        """
        pass
