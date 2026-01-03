const project = new Project('Kompjuta');

await project.addProject(findKore());

project.addFile('sources/**');
project.setDebugDir('deployment');

project.flatten();

resolve(project);
