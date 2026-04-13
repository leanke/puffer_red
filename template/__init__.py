from template.template import TemplateGame

__all__ = ["TemplateGame", "env_creator"]

def env_creator(name="template", *args, **kwargs):
    return TemplateGame
